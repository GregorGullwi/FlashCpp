#pragma once

#include "AstNodeTypes.h"
#include "TemplateRegistry.h"  // For gTemplateRegistry
#include "TypeTraitEvaluator.h"  // For evaluateTypeTrait
#include "TemplateInstantiationHelper.h"  // For shared template instantiation utilities
#include "Log.h"  // For FLASH_LOG
#include <optional>
#include <string>
#include <variant>
#include <climits>  // For LLONG_MAX, LLONG_MIN
#include <charconv>  // For std::from_chars

// Forward declarations
class SymbolTable;
struct TypeInfo;
class Parser;  // For template instantiation

/// @file ConstExprEvaluator.h
/// @brief Constant expression evaluation for static_assert, constexpr variables, etc.
///
/// ## Purpose
///
/// ConstExpr::Evaluator performs **value computation** at compile time.
/// It evaluates expressions to produce primitive values (int, bool, double).
///
/// ## Key Differences from ExpressionSubstitutor
///
/// | Aspect      | ExpressionSubstitutor        | ConstExpr::Evaluator         |
/// |-------------|------------------------------|------------------------------|
/// | Operation   | AST transformation           | Value computation            |
/// | Input       | AST with template params     | AST with concrete types      |
/// | Output      | Modified AST                 | Primitive value (int/bool)   |
/// | When used   | Template instantiation       | static_assert, constexpr     |
///
/// ## Typical Flow
///
/// ```
/// Parser.parse_static_assert()
///   → ConstExpr::Evaluator.evaluate()
///     → evaluate_function_call() → TemplateInstantiationHelper (if template)
///     → evaluate_binary_operator()
///     → evaluate_unary_operator()
///   → EvalResult (bool/int/double value)
/// ```
///
/// @see ExpressionSubstitutor for template parameter substitution
/// @see TemplateInstantiationHelper for shared template instantiation utilities

namespace ConstExpr {

// Error type classification for constexpr evaluation failures
enum class EvalErrorType {
	None,                        // No error (success)
	TemplateDependentExpression, // Error due to template-dependent expression
	NotConstantExpression,       // Expression is not a constant expression
	Other                        // Other types of errors
};

// Result of constant expression evaluation
struct EvalResult {
	std::variant<
		bool,                    // Boolean constant
		long long,               // Signed integer constant
		unsigned long long,      // Unsigned integer constant
		double                   // Floating-point constant
	> value;
	std::string error_message;
	EvalErrorType error_type = EvalErrorType::None;
	
		// Array support for local arrays in constexpr functions
		bool is_array = false;
		std::vector<int64_t> array_values;
		const VariableDeclarationNode* callable_var_decl = nullptr;
		const LambdaExpressionNode* callable_lambda = nullptr;
		std::unordered_map<std::string_view, EvalResult> callable_bindings;
		TypeIndex object_type_index = 0;
		std::unordered_map<std::string_view, EvalResult> object_member_bindings;

	// Check if evaluation was successful
	bool success() const {
		return error_type == EvalErrorType::None;
	}

		// Convenience constructors
		static EvalResult from_bool(bool val) {
			return EvalResult{val, "", EvalErrorType::None, false, {}, nullptr, nullptr, {}, 0, {}};
		}

		static EvalResult from_int(long long val) {
			return EvalResult{val, "", EvalErrorType::None, false, {}, nullptr, nullptr, {}, 0, {}};
		}

		static EvalResult from_uint(unsigned long long val) {
			return EvalResult{val, "", EvalErrorType::None, false, {}, nullptr, nullptr, {}, 0, {}};
		}

		static EvalResult from_double(double val) {
			return EvalResult{val, "", EvalErrorType::None, false, {}, nullptr, nullptr, {}, 0, {}};
		}

		static EvalResult from_callable(const VariableDeclarationNode& var_decl) {
			return EvalResult{0LL, "", EvalErrorType::None, false, {}, &var_decl, nullptr, {}, 0, {}};
		}

		static EvalResult from_lambda(const LambdaExpressionNode& lambda) {
			return EvalResult{0LL, "", EvalErrorType::None, false, {}, nullptr, &lambda, {}, 0, {}};
		}

		static EvalResult error(const std::string& msg, EvalErrorType type = EvalErrorType::Other) {
			return EvalResult{false, msg, type, false, {}, nullptr, nullptr, {}, 0, {}};
		}

	// Convenience helpers for common operations
	bool as_bool() const {
		if (!success()) return false;
		
		// Any non-zero value is true
		if (std::holds_alternative<bool>(value)) {
			return std::get<bool>(value);
		} else if (std::holds_alternative<long long>(value)) {
			return std::get<long long>(value) != 0;
		} else if (std::holds_alternative<unsigned long long>(value)) {
			return std::get<unsigned long long>(value) != 0;
		} else if (std::holds_alternative<double>(value)) {
			return std::get<double>(value) != 0.0;
		}
		return false;
	}

	long long as_int() const {
		if (!success()) return 0;
		
		if (std::holds_alternative<bool>(value)) {
			return std::get<bool>(value) ? 1 : 0;
		} else if (std::holds_alternative<long long>(value)) {
			return std::get<long long>(value);
		} else if (std::holds_alternative<unsigned long long>(value)) {
			return static_cast<long long>(std::get<unsigned long long>(value));
		} else if (std::holds_alternative<double>(value)) {
			return static_cast<long long>(std::get<double>(value));
		}
		return 0;
	}

	double as_double() const {
		if (!success()) return 0.0;
		
		if (std::holds_alternative<bool>(value)) {
			return std::get<bool>(value) ? 1.0 : 0.0;
		} else if (std::holds_alternative<long long>(value)) {
			return static_cast<double>(std::get<long long>(value));
		} else if (std::holds_alternative<unsigned long long>(value)) {
			return static_cast<double>(std::get<unsigned long long>(value));
		} else if (std::holds_alternative<double>(value)) {
			return std::get<double>(value);
		}
		return 0.0;
	}
};

// Storage duration for variable declarations
enum class StorageDuration {
	Automatic,    // Local variables (automatic storage)
	Static,       // Static locals, static members
	Thread,       // thread_local variables
	Global        // Global/namespace scope variables
};

// Context for evaluation - provides access to compile-time information
struct EvaluationContext {
	// Symbol table for looking up constexpr variables/functions (required)
	const SymbolTable* symbols;

	// Global symbol table for looking up global variables (optional)
	const SymbolTable* global_symbols = nullptr;

	// Type information for sizeof, alignof, etc. (future use)
	const TypeInfo* type_info = nullptr;

	// Storage duration of the variable being evaluated (for constinit validation)
	StorageDuration storage_duration = StorageDuration::Automatic;

	// Whether we're evaluating for constinit (requires static/thread storage duration)
	bool is_constinit = false;

	// Complexity limits to prevent infinite loops during evaluation
	size_t step_count = 0;
	size_t max_steps = 1000000;

	// Maximum recursion depth for constexpr functions
	size_t max_recursion_depth = 512;

	// Track current recursion depth
	size_t current_depth = 0;
	
	// Struct being parsed (for looking up static members in static_assert within struct)
	const StructDeclarationNode* struct_node = nullptr;
	const StructTypeInfo* struct_info = nullptr;

	// Template parameter names and arguments for evaluating template-dependent expressions
	// (e.g., sizeof(T) inside a template member function)
	std::vector<std::string_view> template_param_names;
	std::vector<TemplateTypeArg> template_args;
	
	// Parser pointer for template instantiation (optional)
	Parser* parser = nullptr;

	// Constructor requires symbol table to prevent missing it
	explicit EvaluationContext(const SymbolTable& symbol_table)
		: symbols(&symbol_table) {}
};

// Main constant expression evaluator class
class Evaluator {
public:
	static EvalResult evaluate(const ASTNode& expr_node, EvaluationContext& context);

	// Operator evaluation helpers (also used by TemplateInstantiationHelper)
	static EvalResult apply_binary_op(const EvalResult& lhs, const EvalResult& rhs, std::string_view op);
	static EvalResult apply_unary_op(const EvalResult& operand, std::string_view op);

	// Qualified/member access evaluation
	static EvalResult evaluate_qualified_identifier(const QualifiedIdentifierNode& qualified_id, EvaluationContext& context);
	static EvalResult evaluate_member_access(const MemberAccessNode& member_access, EvaluationContext& context);
	static EvalResult evaluate_member_function_call(const MemberFunctionCallNode& member_func_call, EvaluationContext& context);
	static EvalResult evaluate_array_subscript(const ArraySubscriptNode& subscript, EvaluationContext& context);
	static EvalResult evaluate_type_trait(const TypeTraitExprNode& trait_expr);

	static const StructTypeInfo* get_struct_info_from_type(const TypeSpecifierNode& type_spec);
	static EvalResult evaluate_nested_member_access(
		const MemberAccessNode& inner_access,
		std::string_view final_member_name,
		EvaluationContext& context);
	static EvalResult evaluate_array_subscript_member_access(
		[[maybe_unused]] const ArraySubscriptNode& subscript,
		[[maybe_unused]] std::string_view member_name,
		[[maybe_unused]] EvaluationContext& context);
	static EvalResult evaluate_static_member_from_struct(
		const StructTypeInfo* struct_info,
		const TypeInfo& type_info,
		StringHandle member_name_handle,
		std::string_view member_name,
		EvaluationContext& context);
	static EvalResult evaluate_static_member_initializer_or_default(
		const StructStaticMember& static_member,
		EvaluationContext& context);
	static EvalResult evaluate_function_call_member_access(
		const FunctionCallNode& func_call,
		std::string_view member_name,
		EvaluationContext& context);
	static EvalResult extract_object_members(
		const ASTNode& object_expr,
		std::unordered_map<std::string_view, EvalResult>& member_bindings,
		EvaluationContext& context);
	// Shared helper: bind struct members from an InitializerListNode (aggregate init)
	// and apply default member initializers for any members not covered by the list.
		static EvalResult materialize_aggregate_object_value(
			const StructTypeInfo* struct_info,
			TypeIndex type_index,
			const InitializerListNode& init_list,
			EvaluationContext& context);
	static EvalResult bind_members_from_initializer_list(
		const StructTypeInfo* struct_info,
		const InitializerListNode& init_list,
		std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context);
	static EvalResult bind_members_from_constructor_initializers(
		const StructTypeInfo* struct_info,
		const ConstructorDeclarationNode& ctor_decl,
		std::unordered_map<std::string_view, EvalResult>& ctor_param_bindings,
		std::unordered_map<std::string_view, EvalResult>& member_bindings,
		EvaluationContext& context,
		bool ignore_default_initializer_errors);
	static std::optional<EvalResult> try_evaluate_member_from_constructor_initializers(
		const StructTypeInfo* struct_info,
		const ConstructorDeclarationNode& ctor_decl,
		std::unordered_map<std::string_view, EvalResult>& ctor_param_bindings,
		std::string_view member_name,
		EvaluationContext& context);
	static EvalResult evaluate_member_array_subscript(
		const MemberAccessNode& member_access,
		size_t index,
		EvaluationContext& context);
	static EvalResult evaluate_variable_array_subscript(
		const IdentifierNode& identifier,
		size_t index,
		EvaluationContext& context);
	static bool isArithmeticType(Type type);
	static bool isFundamentalType(Type type);

	// Helper struct to hold a ConstructorCallNode reference and its type info
	struct StructObjectInfo {
		const ConstructorCallNode* ctor_call;
		const StructTypeInfo* struct_info;
		const ConstructorDeclarationNode* matching_ctor;
	};

	struct ResolvedConstexprObject {
		const VariableDeclarationNode* var_decl = nullptr;
		const std::optional<ASTNode>* initializer = nullptr;
		TypeIndex declared_type_index{0};
	};

private:
	enum class CurrentStructStaticLookupMode {
		BoundOnly,
		PreferCurrentStruct,
	};

	struct ResolvedCurrentStructStaticMember {
		const StructStaticMember* static_member = nullptr;
		const StructTypeInfo* owner_struct = nullptr;
	};

	struct ResolvedCurrentStructStaticInitializer {
		const std::optional<ASTNode>* initializer = nullptr;
		bool found = false;
	};

	struct ResolvedConstexprMemberSource {
		std::optional<ASTNode> initializer;
		const StructMember* member_info = nullptr;
		std::unordered_map<std::string_view, EvalResult> evaluation_bindings;
	};

	struct ResolvedMemberFunctionCandidate {
		const FunctionDeclarationNode* function = nullptr;
		bool ambiguous = false;
	};

	enum class MemberFunctionLookupMode {
		LookupOnly,
		ConstexprEvaluable,
	};

	// Internal evaluation methods for different node types
	static EvalResult evaluate_numeric_literal(const NumericLiteralNode& literal);
	static EvalResult evaluate_binary_operator(const ASTNode& lhs_node, const ASTNode& rhs_node,
		std::string_view op, EvaluationContext& context);
	static EvalResult evaluate_unary_operator(const ASTNode& operand_node, std::string_view op,
		EvaluationContext& context);
	// get_typespec_size_bytes: unified via getTypeSpecSizeBits (AstNodeTypes_DeclNodes.h)
	static size_t get_typespec_size_bytes(const TypeSpecifierNode& type_spec) {
		return static_cast<size_t>(getTypeSpecSizeBits(type_spec)) / 8;
	}
	static EvalResult evaluate_sizeof(const SizeofExprNode& sizeof_expr, EvaluationContext& context);
	static EvalResult evaluate_alignof(const AlignofExprNode& alignof_expr, EvaluationContext& context);
	static EvalResult evaluate_offsetof(const OffsetofExprNode& offsetof_expr);
	static EvalResult evaluate_noexcept_expr(const NoexceptExprNode& noexcept_expr, EvaluationContext& context);
	static EvalResult evaluate_constructor_call(const ConstructorCallNode& ctor_call, EvaluationContext& context);
	static EvalResult evaluate_static_cast(const StaticCastNode& cast_node, EvaluationContext& context);
	static EvalResult evaluate_expr_node(Type target_type, const ASTNode& expr, EvaluationContext& context, const char* invalidTypeErrorStr);
	static EvalResult evaluate_identifier(const IdentifierNode& identifier, EvaluationContext& context);
	static EvalResult evaluate_ternary_operator(const TernaryOperatorNode& ternary, EvaluationContext& context);
	static bool is_expression_noexcept(const ExpressionNode& expr, EvaluationContext& context);
	static bool is_function_decl_noexcept(const FunctionDeclarationNode& func_decl, EvaluationContext& context);
	static const FunctionDeclarationNode* resolve_function_call_decl(const FunctionCallNode& func_call, EvaluationContext& context);
	static const LambdaExpressionNode* extract_lambda_from_initializer(const std::optional<ASTNode>& initializer);
		static EvalResult materialize_lambda_value(
			const LambdaExpressionNode& lambda,
			EvaluationContext& context,
			const std::unordered_map<std::string_view, EvalResult>* outer_bindings = nullptr);
	// Extract ConstructorCallNode from an initializer, handling direct storage and
	// ExpressionNode-wrapping (e.g., Add() parsed as ExpressionNode(ConstructorCallNode(...))).
	static const ConstructorCallNode* extract_constructor_call(const std::optional<ASTNode>& initializer);
	static EvalResult evaluate_lambda_captures(
		const std::vector<LambdaCaptureNode>& captures,
		std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context,
			const std::unordered_map<std::string_view, EvalResult>* outer_bindings = nullptr,
			const std::unordered_map<std::string_view, EvalResult>* stored_capture_bindings = nullptr);
	static EvalResult evaluate_callable_object(
		const VariableDeclarationNode& var_decl,
		const ChunkedVector<ASTNode>& arguments,
		EvaluationContext& context,
			const std::unordered_map<std::string_view, EvalResult>* outer_bindings = nullptr,
			std::unordered_map<std::string_view, EvalResult>* mutable_outer_bindings = nullptr,
			EvalResult* callable_state = nullptr);
	static EvalResult evaluate_lambda_call(
		const LambdaExpressionNode& lambda,
		const ChunkedVector<ASTNode>& arguments,
		EvaluationContext& context,
			const std::unordered_map<std::string_view, EvalResult>* outer_bindings = nullptr,
			std::unordered_map<std::string_view, EvalResult>* mutable_outer_bindings = nullptr,
			const std::unordered_map<std::string_view, EvalResult>* stored_capture_bindings = nullptr,
			std::unordered_map<std::string_view, EvalResult>* mutable_stored_capture_bindings = nullptr);
	static EvalResult evaluate_builtin_function(std::string_view func_name, const ChunkedVector<ASTNode>& arguments, EvaluationContext& context);
	static EvalResult tryEvaluateAsVariableTemplate(std::string_view func_name, const FunctionCallNode& func_call, EvaluationContext& context);
	static EvalResult evaluate_function_call(const FunctionCallNode& func_call, EvaluationContext& context);
	enum class FunctionCallTemplateBindingLoadMode {
		IfContextEmpty,
		ForceCurrentStructIfAvailable,
	};
	static void load_template_bindings_from_type(const TypeInfo* source_type, EvaluationContext& context);
	static bool try_load_current_struct_template_bindings(EvaluationContext& context);
	static EvalResult evaluate_function_call_with_template_context(
		const FunctionDeclarationNode& func_decl,
		const ChunkedVector<ASTNode>& arguments,
		const std::unordered_map<std::string_view, EvalResult>& outer_bindings,
		EvaluationContext& context,
		const TypeInfo* fallback_template_type = nullptr,
		FunctionCallTemplateBindingLoadMode binding_load_mode = FunctionCallTemplateBindingLoadMode::IfContextEmpty);
	static EvalResult evaluate_function_call_with_bindings(
		const FunctionDeclarationNode& func_decl,
		const ChunkedVector<ASTNode>& arguments,
		const std::unordered_map<std::string_view, EvalResult>& outer_bindings,
		EvaluationContext& context);
	static EvalResult bind_evaluated_arguments(
		const std::vector<ASTNode>& parameters,
		const ChunkedVector<ASTNode>& arguments,
		std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context,
		std::string_view invalid_parameter_error,
		const std::unordered_map<std::string_view, EvalResult>* outer_bindings = nullptr,
		bool skip_invalid_params = false);
	static EvalResult bind_pre_evaluated_arguments(
		const std::vector<ASTNode>& parameters,
		const std::vector<EvalResult>& evaluated_arguments,
		std::unordered_map<std::string_view, EvalResult>& bindings,
		std::string_view invalid_parameter_error,
		bool skip_invalid_params = false);
	static EvalResult evaluate_single_return_block_with_bindings(
		const ASTNode& body_node,
		std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context,
		std::string_view non_block_error,
		std::string_view multi_statement_error);
	static EvalResult evaluate_block_with_bindings(
		const ASTNode& body_node,
		std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context,
		std::string_view non_block_error,
		std::string_view no_return_error);
	static EvalResult evaluate_statement_with_bindings(
		const ASTNode& stmt_node,
		std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context);

	// Expression evaluation with variable bindings (for constexpr function bodies)
	static EvalResult evaluate_expression_with_bindings(
		const ASTNode& expr_node,
		std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context);
	static EvalResult evaluate_expression_with_bindings_const(
		const ASTNode& expr_node,
		const std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context);
	static std::optional<ASTNode> lookup_identifier_symbol(
		const IdentifierNode* identifier,
		std::string_view fallback_name,
		const SymbolTable& symbols);
	static std::optional<ASTNode> lookup_function_symbol(
		const FunctionCallNode& func_call,
		std::string_view fallback_name,
		const SymbolTable& symbols);
	static EvalResult evaluate_function_call_with_outer_bindings(
		const FunctionCallNode& func_call,
		const std::unordered_map<std::string_view, EvalResult>& bindings,
			EvaluationContext& context,
			std::unordered_map<std::string_view, EvalResult>* mutable_bindings = nullptr);
	static std::optional<EvalResult> try_evaluate_bound_member_operator_call(
		const ExpressionNode& expr,
		const std::unordered_map<std::string_view, EvalResult>& bindings,
			EvaluationContext& context,
			std::unordered_map<std::string_view, EvalResult>* mutable_bindings = nullptr);
		static std::optional<EvalResult> try_evaluate_bound_member_access(
			const ExpressionNode& expr,
			const std::unordered_map<std::string_view, EvalResult>& bindings,
			EvaluationContext& context);
		static std::optional<EvalResult> try_evaluate_bound_member_function_call(
			const ExpressionNode& expr,
			const std::unordered_map<std::string_view, EvalResult>& bindings,
			EvaluationContext& context,
			std::unordered_map<std::string_view, EvalResult>* mutable_bindings = nullptr);
	static ResolvedMemberFunctionCandidate find_call_operator_candidate(
		const StructTypeInfo* struct_info,
		size_t argument_count,
		bool detect_ambiguity);
	static ResolvedMemberFunctionCandidate find_member_function_candidate(
		const StructTypeInfo* struct_info,
		StringHandle function_name_handle,
		size_t argument_count,
		EvaluationContext& context,
		MemberFunctionLookupMode lookup_mode,
		bool require_static,
		bool detect_ambiguity);
	static ResolvedMemberFunctionCandidate find_current_struct_member_function_candidate(
		StringHandle function_name_handle,
		size_t argument_count,
		EvaluationContext& context,
		MemberFunctionLookupMode lookup_mode,
		bool require_static,
		bool detect_ambiguity_in_current_struct);
	static std::optional<StringHandle> get_current_struct_static_lookup_name_handle(
		const IdentifierNode* identifier,
		CurrentStructStaticLookupMode lookup_mode);
	static ResolvedCurrentStructStaticMember resolve_current_struct_static_member(
		const IdentifierNode* identifier,
		const EvaluationContext& context,
		CurrentStructStaticLookupMode lookup_mode);
	static ResolvedCurrentStructStaticInitializer resolve_current_struct_static_initializer(
		const IdentifierNode* identifier,
		const EvaluationContext& context,
		CurrentStructStaticLookupMode lookup_mode);
	static std::optional<EvalResult> resolve_constexpr_member_source_from_initializer(
		const std::optional<ASTNode>& object_initializer,
		TypeIndex declared_type_index,
		std::string_view member_name,
		std::string_view usage_name,
		EvaluationContext& context,
		ResolvedConstexprMemberSource& resolved_member,
		const std::unordered_map<std::string_view, EvalResult>* enclosing_bindings = nullptr);
	static std::optional<EvalResult> resolve_constexpr_object_source(
		const IdentifierNode* object_identifier,
		std::string_view object_name,
		EvaluationContext& context,
		std::string_view usage_name,
		ResolvedConstexprObject& resolved_object);
	static const ConstructorDeclarationNode* find_matching_constructor_by_parameter_count(
		const StructTypeInfo* struct_info,
		size_t parameter_count);

	// Safe arithmetic with overflow detection
	static std::optional<long long> safe_add(long long a, long long b);
	static std::optional<long long> safe_sub(long long a, long long b);
	static std::optional<long long> safe_mul(long long a, long long b);
	static std::optional<long long> safe_shl(long long a, long long b);
	static std::optional<long long> safe_shr(long long a, long long b);
};

// Evaluate a fold expression with concrete pack values
// This is used during template instantiation for patterns like:
//   template<bool... Bs> struct __and_ { static constexpr bool value = (Bs && ...); };
// Supported operators: &&, ||, +, *, &, |, ^
// Returns the evaluated result, or nullopt if evaluation fails (e.g., unsupported operator)
// Note: For empty packs, C++17 defines identity values for &&, ||, + and * only.
//       For &, |, ^ with empty packs, this returns nullopt (ill-formed per C++17).
inline std::optional<int64_t> evaluate_fold_expression(std::string_view op, const std::vector<int64_t>& pack_values) {
	// Handle empty packs according to C++17 fold expression semantics
	// Empty packs have identity values for some operators:
	// - (... && pack) with empty pack -> true (1)
	// - (... || pack) with empty pack -> false (0)  
	// - (... + pack) with empty pack -> 0
	// - (... * pack) with empty pack -> 1
	// - Other operators with empty pack are ill-formed
	if (pack_values.empty()) {
		if (op == "&&") return 1;  // true
		if (op == "||") return 0;  // false
		if (op == "+") return 0;
		if (op == "*") return 1;
		// &, |, ^ with empty pack is ill-formed
		return std::nullopt;
	}
	
	std::optional<int64_t> result;
	
	if (op == "&&") {
		result = 1;  // Start with true
		for (int64_t v : pack_values) {
			result = (*result != 0 && v != 0) ? 1 : 0;
			if (*result == 0) break;  // Short-circuit: stop on first false
		}
	} else if (op == "||") {
		result = 0;  // Start with false
		for (int64_t v : pack_values) {
			result = (*result != 0 || v != 0) ? 1 : 0;
			if (*result != 0) break;  // Short-circuit: stop on first true
		}
	} else if (op == "+") {
		result = 0;
		for (int64_t v : pack_values) {
			*result += v;
		}
	} else if (op == "*") {
		result = 1;
		for (int64_t v : pack_values) {
			*result *= v;
		}
	} else if (op == "&") {
		result = pack_values[0];
		for (size_t i = 1; i < pack_values.size(); ++i) {
			*result &= pack_values[i];
		}
	} else if (op == "|") {
		result = pack_values[0];
		for (size_t i = 1; i < pack_values.size(); ++i) {
			*result |= pack_values[i];
		}
	} else if (op == "^") {
		result = pack_values[0];
		for (size_t i = 1; i < pack_values.size(); ++i) {
			*result ^= pack_values[i];
		}
	}
	// Unsupported operator returns nullopt (result stays unset)
	
	return result;
}

} // namespace ConstExpr
