#pragma once

#include "AstNodeTypes.h"
#include "TemplateRegistry.h"  // For gTemplateRegistry
#include "TypeTraitEvaluator.h"  // For evaluateTypeTrait
#include "TemplateInstantiationHelper.h"  // For shared template instantiation utilities
#include "IROperandHelpers.h"  // For isCompoundAssignmentOp / kCompoundOpTable
#include "StringBuilder.h"  // For StringBuilder (heap key construction)
#include "Log.h"  // For FLASH_LOG
#include "InlineVector.h"  // For InlineVector (small-buffer-optimized vector)
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
	std::vector<EvalResult> array_elements;
	std::vector<int64_t> array_values;
	const VariableDeclarationNode* callable_var_decl = nullptr;
	const LambdaExpressionNode* callable_lambda = nullptr;
	std::unordered_map<std::string_view, EvalResult> callable_bindings;
	std::optional<TypeSpecifierNode> exact_type;
	TypeIndex object_type_index {};
	std::unordered_map<std::string_view, EvalResult> object_member_bindings;
	// Constexpr pointer support: when valid, this result represents a pointer
	// to a named constexpr variable (produced by the address-of operator &identifier).
	// Uses StringHandle (lightweight 32-bit integer) instead of std::string to avoid
	// heap allocation overhead on every EvalResult copy.
	StringHandle pointer_to_var;
	// Element offset from the base variable for pointer arithmetic.
	// When pointer_to_var is valid and pointer_offset != 0, this pointer refers to
	// element [pointer_offset] of the array variable named by pointer_to_var
	// (e.g. &arr[2] yields pointer_to_var="arr", pointer_offset=2).
	int64_t pointer_offset = 0;
	// When is_array is true and this handle is valid, it records the binding-map key
	// that this array was loaded from. Enables array-to-pointer decay for the pattern
	// `return data + N;` inside a constexpr member function where `data` is a member array.
	StringHandle array_origin_var;

	// Check if evaluation was successful
	bool success() const {
		return error_type == EvalErrorType::None;
	}

	// Convenience constructors
	static EvalResult from_bool(bool val) {
		return EvalResult{val, "", EvalErrorType::None, false, {}, {}, nullptr, nullptr, {}, {}, TypeIndex{}, {}, {}, 0, {}};
	}

	static EvalResult from_int(long long val) {
		return EvalResult{val, "", EvalErrorType::None, false, {}, {}, nullptr, nullptr, {}, {}, TypeIndex{}, {}, {}, 0, {}};
	}

	static EvalResult from_uint(unsigned long long val) {
		return EvalResult{val, "", EvalErrorType::None, false, {}, {}, nullptr, nullptr, {}, {}, TypeIndex{}, {}, {}, 0, {}};
	}

	static EvalResult from_double(double val) {
		return EvalResult{val, "", EvalErrorType::None, false, {}, {}, nullptr, nullptr, {}, {}, TypeIndex{}, {}, {}, 0, {}};
	}

	static EvalResult from_callable(const VariableDeclarationNode& var_decl) {
		return EvalResult{0LL, "", EvalErrorType::None, false, {}, {}, &var_decl, nullptr, {}, {}, TypeIndex{}, {}, {}, 0, {}};
	}

	static EvalResult from_lambda(const LambdaExpressionNode& lambda) {
		return EvalResult{0LL, "", EvalErrorType::None, false, {}, {}, nullptr, &lambda, {}, {}, TypeIndex{}, {}, {}, 0, {}};
	}

	static EvalResult error(const std::string& msg, EvalErrorType type = EvalErrorType::Other) {
		return EvalResult{false, msg, type, false, {}, {}, nullptr, nullptr, {}, {}, TypeIndex{}, {}, {}, 0, {}};
	}

	// Create a pointer-to-variable result (for address-of operator on constexpr variables).
	// offset is the element offset for pointer arithmetic (e.g. &arr[2] → offset=2).
	static EvalResult from_pointer(std::string_view var_name, int64_t offset = 0) {
		EvalResult r{0LL, "", EvalErrorType::None, false, {}, {}, nullptr, nullptr, {}, {}, TypeIndex{}, {}, StringTable::getOrInternStringHandle(var_name), offset, {}};
		return r;
	}

	// Overload that accepts an already-interned StringHandle directly (avoids double interning).
	static EvalResult from_pointer(StringHandle sh, int64_t offset = 0) {
		EvalResult r{0LL, "", EvalErrorType::None, false, {}, {}, nullptr, nullptr, {}, {}, TypeIndex{}, {}, sh, offset, {}};
		return r;
	}

	EvalResult& set_exact_type(const TypeSpecifierNode& type) {
		exact_type = type;
		return *this;
	}

	// Convenience helpers for common operations
	bool as_bool() const {
		if (!success()) return false;

		// A valid non-null constexpr pointer is truthy (matches C++ semantics for if(ptr)).
		// pointer_to_var.isValid() is true when the StringHandle holds an interned variable
		// name (i.e., the pointer was produced by &identifier and points to a known variable).
		if (pointer_to_var.isValid()) return true;

		// Any non-zero value is true
		if (const auto* b_val = std::get_if<bool>(&value)) {
			return *b_val;
		} else if (const auto* ll_val = std::get_if<long long>(&value)) {
			return *ll_val != 0;
		} else if (const auto* ull_val = std::get_if<unsigned long long>(&value)) {
			return *ull_val != 0;
		} else if (const auto* d_val = std::get_if<double>(&value)) {
			return *d_val != 0.0;
		}
		return false;
	}

	long long as_int() const {
		if (!success()) return 0;
		
		if (const auto* b_val = std::get_if<bool>(&value)) {
			return *b_val ? 1 : 0;
		} else if (const auto* ll_val = std::get_if<long long>(&value)) {
			return *ll_val;
		} else if (const auto* ull_val = std::get_if<unsigned long long>(&value)) {
			return static_cast<long long>(*ull_val);
		} else if (const auto* d_val = std::get_if<double>(&value)) {
			return static_cast<long long>(*d_val);
		}
		return 0;
	}

	// Returns true when the stored variant holds an unsigned long long.
	// Prefer this over std::holds_alternative<unsigned long long>(value)
	// for consistency with the rest of the helpers.
	bool is_uint() const {
		return std::get_if<unsigned long long>(&value) != nullptr;
	}

	// Extracts the raw unsigned bit pattern without a signed round-trip.
	// When the value is already unsigned long long it is returned directly,
	// avoiding the sign-extension that as_int() would introduce for values
	// above LLONG_MAX.  For all other types the result is a zero-extending
	// reinterpretation (same as static_cast<unsigned long long>(as_int())).
	unsigned long long as_uint_raw() const {
		if (const auto* ull_val = std::get_if<unsigned long long>(&value)) {
			return *ull_val;
		} else if (const auto* ll_val = std::get_if<long long>(&value)) {
			return static_cast<unsigned long long>(*ll_val);
		} else if (const auto* b_val = std::get_if<bool>(&value)) {
			return *b_val ? 1ULL : 0ULL;
		} else if (const auto* d_val = std::get_if<double>(&value)) {
			return static_cast<unsigned long long>(*d_val);
		}
		return 0;
	}

	double as_double() const {
		if (!success()) return 0.0;
		
		if (const auto* b_val = std::get_if<bool>(&value)) {
			return *b_val ? 1.0 : 0.0;
		} else if (const auto* ll_val = std::get_if<long long>(&value)) {
			return static_cast<double>(*ll_val);
		} else if (const auto* ull_val = std::get_if<unsigned long long>(&value)) {
			return static_cast<double>(*ull_val);
		} else if (const auto* d_val = std::get_if<double>(&value)) {
			return *d_val;
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

// Tracks variables declared in a single block scope so that the block
// can clean them up on exit.  Each nested block level has its own tracker.
// This enables proper C++ variable scoping: new declarations are removed
// when the block exits, and variables that were shadowed (a declaration
// in the inner block used the same name as an outer-scope variable) are
// restored to their pre-block values.  Mutations to outer-scope variables
// (e.g. `sum += i` inside a loop body) are NOT reverted — only declarations
// trigger the cleanup logic.
struct BlockScopeTracker {
	// Names declared in this block scope (in declaration order).
	// Most scopes declare 0–3 variables; InlineVector avoids heap allocation
	// for the common case.
	InlineVector<std::string_view, 4> declared_names;
	// For each name that shadowed an existing binding, the saved outer value.
	std::unordered_map<std::string_view, EvalResult> saved_shadows;

	// Called by the VariableDeclarationNode handler before writing the
	// new binding into the flat map.  Pass the current bindings so that
	// shadowing can be detected and the old value saved.
	void on_declare(std::string_view name,
	                const std::unordered_map<std::string_view, EvalResult>& bindings) {
		// Only track the first declaration of this name in this scope.
		// Subsequent re-declarations (e.g. loop body without braces across
		// iterations) must not overwrite the original shadow decision:
		// if the name was brand-new on the first call, it should still be
		// erased (not restored) at cleanup.
		if (saved_shadows.find(name) != saved_shadows.end()) {
			return;  // Already tracked with a saved shadow — first decision wins.
		}
		if (std::find(declared_names.begin(), declared_names.end(), name) != declared_names.end()) {
			return;  // Already declared in this scope as a new variable.
		}
		declared_names.push_back(name);
		auto it = bindings.find(name);
		if (it != bindings.end()) {
			// This declaration shadows an outer-scope binding — save the
			// original value so the block exit can restore it.
			saved_shadows.emplace(name, it->second);
		}
	}

	// Called at block exit: remove new declarations and restore shadows.
	// The method is const with respect to the tracker's own state; it only
	// modifies the external bindings map passed by reference.
	void cleanup(std::unordered_map<std::string_view, EvalResult>& bindings) const {
		for (const auto& name : declared_names) {
			auto shadow_it = saved_shadows.find(name);
			if (shadow_it != saved_shadows.end()) {
				// Restore the pre-block (outer) binding value.
				bindings[name] = shadow_it->second;
			} else {
				// Brand-new variable — remove it so it does not leak.
				bindings.erase(name);
			}
		}
	}
};

// RAII guard that installs a BlockScopeTracker as the active scope in an
// EvaluationContext and automatically cleans up declared variables (and
// restores shadowed outer values) when it goes out of scope.  This allows
// early returns from control-flow handlers without manually calling cleanup.
struct BlockScopeGuard {
	BlockScopeTracker scope;
	std::unordered_map<std::string_view, EvalResult>& bindings;
	BlockScopeTracker*& context_current_scope;
	BlockScopeTracker* const outer_scope;

	explicit BlockScopeGuard(std::unordered_map<std::string_view, EvalResult>& b,
	                          BlockScopeTracker*& ctx_scope)
		: bindings(b), context_current_scope(ctx_scope), outer_scope(ctx_scope) {
		context_current_scope = &scope;
	}

	~BlockScopeGuard() {
		scope.cleanup(bindings);
		context_current_scope = outer_scope;
	}

	// Non-copyable, non-movable.
	BlockScopeGuard(const BlockScopeGuard&) = delete;
	BlockScopeGuard& operator=(const BlockScopeGuard&) = delete;
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
	// Cached type index for the current struct (parallel to struct_info; avoids O(n) search in gTypeInfo).
	TypeIndex struct_type_index {};
	std::unordered_map<std::string_view, EvalResult>* local_bindings = nullptr;

	// Pointer to the innermost active block scope tracker (null at top level).
	// Each BlockScopeGuard saves/restores this so that nested blocks each get
	// their own tracker.
	BlockScopeTracker* current_scope = nullptr;

	// Returns the map that variable declarations should be written to (and
	// that BlockScopeGuard / on_declare should target).  When local_bindings
	// is set (e.g. constructor body evaluation), declarations go there;
	// otherwise they go to the regular bindings map passed by the caller.
	std::unordered_map<std::string_view, EvalResult>& resolve_declaration_bindings(
		std::unordered_map<std::string_view, EvalResult>& bindings) {
		return local_bindings ? *local_bindings : bindings;
	}

	// Template parameter names and arguments for evaluating template-dependent expressions
	// (e.g., sizeof(T) inside a template member function)
	std::vector<std::string_view> template_param_names;
	std::vector<TemplateTypeArg> template_args;
	
	// Parser pointer for template instantiation (optional)
	Parser* parser = nullptr;

	// Return type of the constexpr function currently being evaluated.
	// Set by evaluate_function_call_with_bindings so that aggregate initializer
	// return expressions (e.g., return {0, 0} in a struct-returning function)
	// can be mapped to the correct struct member names.
	const TypeInfo* return_type_info = nullptr;

	// When true, short-circuit evaluation for && and || is disabled.
	// Set by try_evaluate_constant_expression to prevent false positive results
	// during template-argument disambiguation (where a truthy LHS of `||` would
	// cause the speculative parse to succeed, incorrectly treating `<` as a
	// template-argument-list opener).
	bool is_speculative = false;

	// Constexpr heap: tracks objects dynamically allocated with `new` inside a
	// constant expression (C++20 [expr.const]/p5).  Each entry maps a synthetic
	// key (e.g. "@new_0") to the allocated value and a freed flag.
	// `delete ptr` marks the corresponding entry freed; at the end of a
	// well-formed constant expression all allocations must have been freed.
	struct ConstexprHeapEntry {
		EvalResult value;
		bool freed = false;
		bool is_array = false;
	};
	std::unordered_map<StringHandle, ConstexprHeapEntry, StringHash, StringEqual> constexpr_heap;
	size_t next_heap_id = 0;

	// Allocate a fresh synthetic heap key, intern it, and return its StringHandle.
	StringHandle alloc_heap_slot() {
		return StringTable::getOrInternStringHandle(
			StringBuilder().append("@new_"sv).append(static_cast<uint64_t>(next_heap_id++)).commit());
	}

	// Returns true iff any allocation that was made with `new` during this
	// constant expression evaluation has not yet been freed with `delete`.
	// Per C++20 [expr.const]/p5 this makes the expression ill-formed.
	bool has_unfreed_heap_allocations() const {
		for (const auto& [key, entry] : constexpr_heap) {
			if (!entry.freed) return true;
		}
		return false;
	}

	// Constructor requires symbol table to prevent missing it
	explicit EvaluationContext(const SymbolTable& symbol_table)
		: symbols(&symbol_table) {}
};

// Main constant expression evaluator class
class Evaluator {
public:
	static EvalResult evaluate(const ASTNode& expr_node, EvaluationContext& context);

	// Operator evaluation helpers (also used by TemplateInstantiationHelper)
	static EvalResult apply_binary_op(
		const EvalResult& lhs, const EvalResult& rhs, std::string_view op,
		EvaluationContext* context = nullptr,
		const std::unordered_map<std::string_view, EvalResult>* bindings = nullptr);
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
			EvaluationContext& context,
			const std::unordered_map<std::string_view, EvalResult>* outer_bindings = nullptr);
		static EvalResult materialize_constructor_object_value(
			const ConstructorCallNode& ctor_call,
			EvaluationContext& context,
			const std::unordered_map<std::string_view, EvalResult>* outer_bindings = nullptr);
		static EvalResult materialize_array_value(
			TypeIndex element_type_index,
			const InitializerListNode& init_list,
			EvaluationContext& context,
			const std::unordered_map<std::string_view, EvalResult>* bindings = nullptr);
		// Variant that accepts the full TypeSpecifierNode so that multi-dimensional arrays
		// (e.g., int[2][3]) can be materialised with proper inner-dimension sizes even when
		// the initializer list is shorter than the outer dimension (zero-padding) or contains
		// plain scalar initialisers that should initialise a nested row.
		static EvalResult materialize_array_value_with_spec(
			const TypeSpecifierNode& type_spec,
			const InitializerListNode& init_list,
			EvaluationContext& context,
			const std::unordered_map<std::string_view, EvalResult>* bindings = nullptr);
		static EvalResult bind_members_from_initializer_list(
			const StructTypeInfo* struct_info,
			const InitializerListNode& init_list,
			std::unordered_map<std::string_view, EvalResult>& bindings,
			EvaluationContext& context,
			const std::unordered_map<std::string_view, EvalResult>* evaluation_bindings = nullptr);
		static EvalResult bind_members_from_constructor_initializers(
			const StructTypeInfo* struct_info,
			const ConstructorDeclarationNode& ctor_decl,
			std::unordered_map<std::string_view, EvalResult>& ctor_param_bindings,
			std::unordered_map<std::string_view, EvalResult>& member_bindings,
			EvaluationContext& context,
			bool ignore_default_initializer_errors);
		static EvalResult materialize_members_from_constructor(
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
		// Attempt to materialize a struct object by finding and invoking a matching
		// user-defined constructor with the given arguments.  Returns std::nullopt when
		// no matching constructor exists (caller may fall back to aggregate init).
		// Returns an EvalResult (success or error) when a constructor candidate was found
		// and materialization was attempted.
		static std::optional<EvalResult> try_materialize_struct_from_ctor_args(
			const StructTypeInfo* struct_info,
			TypeIndex type_index,
			const ChunkedVector<ASTNode>& args,
			EvaluationContext& context,
			const std::unordered_map<std::string_view, EvalResult>* outer_bindings = nullptr);
		static EvalResult evaluate_member_array_subscript(
			const MemberAccessNode& member_access,
			size_t index,
			EvaluationContext& context);
		static EvalResult evaluate_variable_array_subscript(
			const IdentifierNode& identifier,
			size_t index,
			EvaluationContext& context);
		static bool isArithmeticType(TypeCategory cat);
		static bool isFundamentalType(TypeCategory cat);

	// Helper struct to hold a ConstructorCallNode reference and its type info
	struct StructObjectInfo {
		const ConstructorCallNode* ctor_call;
		const StructTypeInfo* struct_info;
		const ConstructorDeclarationNode* matching_ctor;
	};

	struct ResolvedConstexprObject {
		const VariableDeclarationNode* var_decl = nullptr;
		const std::optional<ASTNode>* initializer = nullptr;
		TypeIndex declared_type_index{};
	};

	struct ExtractedIdentifier {
		const IdentifierNode* identifier = nullptr;
		std::string_view name;
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
			std::optional<EvalResult> value;
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
	// Dereference a constexpr pointer: look up the named variable in the symbol table and evaluate it.
	// When offset != 0, the variable must be an array and element [offset] is returned.
	static EvalResult dereference_constexpr_pointer(std::string_view var_name, EvaluationContext& context, int64_t offset = 0);
	// Dereference a pointer result against local bindings first, then the symbol table.
	// Handles scalars (offset == 0) and arrays (any offset).
	static EvalResult deref_pointer_with_bindings(
		const EvalResult& ptr, const std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context);
	// Shared helper for arrow member access (ptr->member) where pointed_name is the name of the
	// pointed-to constexpr variable.  Resolves the variable, extracts the requested member, and
	// evaluates it.  If check_static is true, also handles access to static struct members.
	static EvalResult evaluate_arrow_member_from_pointer_var(
		std::string_view pointed_name, std::string_view member_name,
		EvaluationContext& context, bool check_static = false);
	// get_typespec_size_bytes: unified via getTypeSpecSizeBits (AstNodeTypes_DeclNodes.h)
	static size_t get_typespec_size_bytes(const TypeSpecifierNode& type_spec) {
		return static_cast<size_t>(getTypeSpecSizeBits(type_spec)) / 8;
	}
	static EvalResult evaluate_sizeof(const SizeofExprNode& sizeof_expr, EvaluationContext& context);
	static EvalResult evaluate_alignof(const AlignofExprNode& alignof_expr, EvaluationContext& context);
	static EvalResult evaluate_offsetof(const OffsetofExprNode& offsetof_expr);
	static EvalResult evaluate_noexcept_expr(const NoexceptExprNode& noexcept_expr, EvaluationContext& context);
	static EvalResult evaluate_constructor_call(const ConstructorCallNode& ctor_call, EvaluationContext& context);
	static EvalResult evaluate_new_expression(const NewExpressionNode& new_expr, EvaluationContext& context,
		const std::unordered_map<std::string_view, EvalResult>* bindings = nullptr);
	static EvalResult evaluate_delete_expression(const DeleteExpressionNode& del_expr, EvaluationContext& context,
		const std::unordered_map<std::string_view, EvalResult>* bindings = nullptr);
	static EvalResult evaluate_static_cast(const StaticCastNode& cast_node, EvaluationContext& context);
	static EvalResult evaluate_const_cast(const ConstCastNode& cast_node, EvaluationContext& context);
	static EvalResult evaluate_expr_node(const TypeSpecifierNode& target_type, const ASTNode& expr, EvaluationContext& context, const char* invalidTypeErrorStr);
	static EvalResult evaluate_identifier(const IdentifierNode& identifier, EvaluationContext& context);
	static EvalResult evaluate_ternary_operator(const TernaryOperatorNode& ternary, EvaluationContext& context);
	static bool is_expression_noexcept(const ExpressionNode& expr, EvaluationContext& context);
	static bool is_function_decl_noexcept(const FunctionDeclarationNode& func_decl, EvaluationContext& context);
	static const FunctionDeclarationNode* resolve_function_call_decl(const FunctionCallNode& func_call, EvaluationContext& context);
	static const LambdaExpressionNode* extract_lambda_from_initializer(const std::optional<ASTNode>& initializer);
	static std::optional<ExtractedIdentifier> extract_identifier_from_expression(const ASTNode& object_expr);
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
	using RecursiveBindEvalFn = EvalResult(*)(const ASTNode&, const std::unordered_map<std::string_view, EvalResult>&, EvaluationContext&);
	static EvalResult evaluate_expression_with_bindings_dispatch(
		const ASTNode& expr_node,
		const std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context,
		RecursiveBindEvalFn recursive_eval,
		std::unordered_map<std::string_view, EvalResult>* mutable_bindings);
	static std::optional<ASTNode> lookup_identifier_symbol(
		const IdentifierNode* identifier,
		std::string_view fallback_name,
		const SymbolTable& symbols);
	// Returns true if the identifier resolves to a declared array variable (not a pointer).
	// Used by evaluate_array_subscript to route array and pointer subscripts correctly.
	static bool identifier_is_array_var(const IdentifierNode& id, EvaluationContext& context);
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
		struct ResolvedBoundEvalResult {
			const EvalResult* value = nullptr;
			std::optional<EvalResult> owned_value;
			std::optional<EvalResult> error;
		};
		static ResolvedBoundEvalResult resolve_bound_eval_result(
			const ASTNode& bound_expr,
			const std::unordered_map<std::string_view, EvalResult>& bindings,
			EvaluationContext& context,
			bool treat_this_as_unbound = false);
		static std::optional<EvalResult> try_evaluate_bound_member_access(
			const ExpressionNode& expr,
			const std::unordered_map<std::string_view, EvalResult>& bindings,
			EvaluationContext& context);
		static std::optional<EvalResult> try_evaluate_bound_array_subscript(
			const ExpressionNode& expr,
			const std::unordered_map<std::string_view, EvalResult>& bindings,
			EvaluationContext& context);
		static std::optional<EvalResult> try_evaluate_bound_member_function_call(
			const ExpressionNode& expr,
			const std::unordered_map<std::string_view, EvalResult>& bindings,
			EvaluationContext& context,
			std::unordered_map<std::string_view, EvalResult>* mutable_bindings = nullptr);
	// Call a 0-argument named constexpr member function on an already-evaluated object
	// EvalResult (one with object_type_index and object_member_bindings populated).
	// For template instantiations whose member-function stubs lack a body, this helper
	// automatically falls back to the base template's StructTypeInfo to find the function
	// definition. Template parameter bindings are saved and restored around the call.
	// Returns the return value of the member function, or an error EvalResult on failure.
	static EvalResult call_constexpr_member_fn_on_object(
		const EvalResult& object,
		std::string_view func_name,
		EvaluationContext& context);
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
		static const ConstructorDeclarationNode* find_matching_constructor(
		const StructTypeInfo* struct_info,
			const ChunkedVector<ASTNode>& arguments,
			EvaluationContext& context,
			const std::unordered_map<std::string_view, EvalResult>* outer_bindings = nullptr);

	// Type comparison helpers — shared across Core and Members TUs.
	// Compares two TypeSpecifierNodes ignoring cv-qualifiers (and optionally
	// reference qualifiers), checking type(), type_index(), pointer_depth(),
	// array_dimensions(), member class, and function signatures.
	static bool typesMatchIgnoringCvAndRef(const TypeSpecifierNode& lhs, const TypeSpecifierNode& rhs);

	// Try to obtain the source expression's type from an already-evaluated
	// result (exact_type) or by asking the parser for the AST node's type.
	static std::optional<TypeSpecifierNode> tryGetExpressionType(
		const EvalResult& result,
		const ASTNode& expr,
		EvaluationContext& context);

	// Convert an already-evaluated EvalResult to a different target type
	// (Bool/Int/Uint/Float).  Returns an error for unsupported target types
	// (e.g. Struct).  Shared across Core and Members TUs.
	static EvalResult convertEvalResultToTargetType(
		const TypeSpecifierNode& target_type,
		const EvalResult& expr_result,
		const char* invalidTypeErrorStr);

	// Safe arithmetic with overflow detection
	static std::optional<long long> safe_add(long long a, long long b);
	static std::optional<long long> safe_sub(long long a, long long b);
	static std::optional<long long> safe_mul(long long a, long long b);
	static std::optional<long long> safe_shl(long long a, long long b, int width_bits = 64);
	static std::optional<long long> safe_shr(long long a, long long b, int width_bits = 64);
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
