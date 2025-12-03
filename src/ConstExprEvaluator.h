#pragma once

#include "AstNodeTypes.h"
#include <optional>
#include <string>
#include <variant>
#include <climits>  // For LLONG_MAX, LLONG_MIN

// Forward declarations
class SymbolTable;
struct TypeInfo;

namespace ConstExpr {

// Result of constant expression evaluation
struct EvalResult {
	bool success;
	std::variant<
		bool,                    // Boolean constant
		long long,               // Signed integer constant
		unsigned long long,      // Unsigned integer constant
		double                   // Floating-point constant
	> value;
	std::string error_message;

	// Convenience constructors
	static EvalResult from_bool(bool val) {
		return EvalResult{true, val, ""};
	}

	static EvalResult from_int(long long val) {
		return EvalResult{true, val, ""};
	}

	static EvalResult from_uint(unsigned long long val) {
		return EvalResult{true, val, ""};
	}

	static EvalResult from_double(double val) {
		return EvalResult{true, val, ""};
	}

	static EvalResult error(const std::string& msg) {
		return EvalResult{false, false, msg};
	}

	// Convenience helpers for common operations
	bool as_bool() const {
		if (!success) return false;
		
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
		if (!success) return 0;
		
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
		if (!success) return 0.0;
		
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

	// Constructor requires symbol table to prevent missing it
	explicit EvaluationContext(const SymbolTable& symbol_table)
		: symbols(&symbol_table) {}
};

// Main constant expression evaluator class
class Evaluator {
public:
	// Main evaluation entry point
	// Evaluates a constant expression and returns the result
	static EvalResult evaluate(const ASTNode& expr_node, EvaluationContext& context) {
		// Check complexity limit
		if (++context.step_count > context.max_steps) {
			return EvalResult::error("Constexpr evaluation exceeded complexity limit (infinite loop?)");
		}

		// Evaluate a constant expression
		// Returns the result or an error if not a constant expression

		// The expr_node should be an ExpressionNode variant
		if (!expr_node.is<ExpressionNode>()) {
			return EvalResult::error("AST node is not an expression");
		}

		const ExpressionNode& expr = expr_node.as<ExpressionNode>();

		// Check what type of expression it is
		if (std::holds_alternative<NumericLiteralNode>(expr)) {
			return evaluate_numeric_literal(std::get<NumericLiteralNode>(expr));
		}

		// For BinaryOperatorNode, we need to check if it's in the variant
		if (std::holds_alternative<BinaryOperatorNode>(expr)) {
			const auto& bin_op = std::get<BinaryOperatorNode>(expr);
			return evaluate_binary_operator(bin_op.get_lhs(), bin_op.get_rhs(), bin_op.op(), context);
		}

		// For UnaryOperatorNode
		if (std::holds_alternative<UnaryOperatorNode>(expr)) {
			const auto& unary_op = std::get<UnaryOperatorNode>(expr);
			return evaluate_unary_operator(unary_op.get_operand(), unary_op.op(), context);
		}

		// For SizeofExprNode
		if (std::holds_alternative<SizeofExprNode>(expr)) {
			return evaluate_sizeof(std::get<SizeofExprNode>(expr), context);
		}

		// For ConstructorCallNode (type conversions like float(3.14), int(100))
		if (std::holds_alternative<ConstructorCallNode>(expr)) {
			return evaluate_constructor_call(std::get<ConstructorCallNode>(expr), context);
		}

		// For IdentifierNode (variable references like 'x' in 'constexpr int y = x + 1;')
		if (std::holds_alternative<IdentifierNode>(expr)) {
			return evaluate_identifier(std::get<IdentifierNode>(expr), context);
		}

		// For TernaryOperatorNode (condition ? true_expr : false_expr)
		if (std::holds_alternative<TernaryOperatorNode>(expr)) {
			return evaluate_ternary_operator(std::get<TernaryOperatorNode>(expr), context);
		}

		// For FunctionCallNode (constexpr function calls)
		if (std::holds_alternative<FunctionCallNode>(expr)) {
			return evaluate_function_call(std::get<FunctionCallNode>(expr), context);
		}

		// For QualifiedIdentifierNode (e.g., Template<T>::member)
		if (std::holds_alternative<QualifiedIdentifierNode>(expr)) {
			return evaluate_qualified_identifier(std::get<QualifiedIdentifierNode>(expr), context);
		}

		// For MemberAccessNode (e.g., obj.member or ptr->member)
		if (std::holds_alternative<MemberAccessNode>(expr)) {
			return evaluate_member_access(std::get<MemberAccessNode>(expr), context);
		}

		// For StaticCastNode (static_cast<Type>(expr) and C-style casts)
		if (std::holds_alternative<StaticCastNode>(expr)) {
			return evaluate_static_cast(std::get<StaticCastNode>(expr), context);
		}

		// Other expression types are not supported as constant expressions yet
		return EvalResult::error("Expression type not supported in constant expressions");
	}

private:
	// Internal evaluation methods for different node types
	static EvalResult evaluate_numeric_literal(const NumericLiteralNode& literal) {
		const auto& value = literal.value();

		if (std::holds_alternative<unsigned long long>(value)) {
			unsigned long long val = std::get<unsigned long long>(value);
			return EvalResult::from_uint(val);
		} else if (std::holds_alternative<double>(value)) {
			double val = std::get<double>(value);
			return EvalResult::from_double(val);
		}

		return EvalResult::error("Unknown numeric literal type");
	}

	static EvalResult evaluate_binary_operator(const ASTNode& lhs_node, const ASTNode& rhs_node, 
	                                            std::string_view op, EvaluationContext& context) {
		// Recursively evaluate left and right operands
		auto lhs_result = evaluate(lhs_node, context);
		auto rhs_result = evaluate(rhs_node, context);

		if (!lhs_result.success) {
			return lhs_result;
		}
		if (!rhs_result.success) {
			return rhs_result;
		}

		return apply_binary_op(lhs_result, rhs_result, op);
	}

	static EvalResult evaluate_unary_operator(const ASTNode& operand_node, std::string_view op,
	                                           EvaluationContext& context) {
		// Recursively evaluate operand
		auto operand_result = evaluate(operand_node, context);

		if (!operand_result.success) {
			return operand_result;
		}

		return apply_unary_op(operand_result, op);
	}

	static EvalResult evaluate_sizeof(const SizeofExprNode& sizeof_expr, EvaluationContext& context) {
		// sizeof is always a constant expression
		// Get the actual size from the type
		if (sizeof_expr.is_type()) {
			// sizeof(type) - get size from TypeSpecifierNode
			const auto& type_node = sizeof_expr.type_or_expr();
			if (type_node.is<TypeSpecifierNode>()) {
				const auto& type_spec = type_node.as<TypeSpecifierNode>();
				// size_in_bits() returns bits, convert to bytes
				unsigned long long size_in_bytes = type_spec.size_in_bits() / 8;
				return EvalResult::from_int(static_cast<long long>(size_in_bytes));
			}
		}
		
		// For sizeof(expression), we would need to evaluate the expression type
		// For now, just return an error
		return EvalResult::error("sizeof with expression not yet supported");
	}

	static EvalResult evaluate_constructor_call(const ConstructorCallNode& ctor_call, EvaluationContext& context) {
		// Constructor calls like float(3.14), int(100), double(2.718)
		// These are essentially type conversions/casts in constant expressions
		
		// Get the target type
		const ASTNode& type_node = ctor_call.type_node();
		if (!type_node.is<TypeSpecifierNode>()) {
			return EvalResult::error("Constructor call without valid type specifier");
		}
		
		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
		Type target_type = type_spec.type();
		
		// Get the argument(s) - for basic type conversions, should have exactly 1 argument
		const auto& args = ctor_call.arguments();
		if (args.size() != 1) {
			return EvalResult::error("Constructor call must have exactly 1 argument for constant evaluation");
		}
		
		// Evaluate the argument
		const ASTNode& arg = args[0];
		auto arg_result = evaluate(arg, context);
		if (!arg_result.success) {
			return arg_result;
		}
		
		// Convert to target type
		switch (target_type) {
			case Type::Bool:
				return EvalResult::from_bool(arg_result.as_bool());
			
			case Type::Char:
			case Type::Short:
			case Type::Int:
			case Type::Long:
			case Type::LongLong:
				return EvalResult::from_int(arg_result.as_int());
			
			case Type::UnsignedChar:
			case Type::UnsignedShort:
			case Type::UnsignedInt:
			case Type::UnsignedLong:
			case Type::UnsignedLongLong:
				// For unsigned types, convert to unsigned
				return EvalResult::from_uint(static_cast<unsigned long long>(arg_result.as_int()));
			
			case Type::Float:
			case Type::Double:
			case Type::LongDouble:
				return EvalResult::from_double(arg_result.as_double());
			
		default:
			return EvalResult::error("Unsupported type in constructor call for constant evaluation");
		}
	}

	static EvalResult evaluate_static_cast(const StaticCastNode& cast_node, EvaluationContext& context) {
		// Evaluate static_cast<Type>(expr) and C-style casts in constant expressions
		
		// Get the target type
		const ASTNode& type_node = cast_node.target_type();
		if (!type_node.is<TypeSpecifierNode>()) {
			return EvalResult::error("Cast without valid type specifier");
		}
		
		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
		Type target_type = type_spec.type();
		
		// Evaluate the expression being cast
		const ASTNode& expr = cast_node.expr();
		auto expr_result = evaluate(expr, context);
		if (!expr_result.success) {
			return expr_result;
		}
		
		// Perform the type conversion
		switch (target_type) {
			case Type::Bool:
				return EvalResult::from_bool(expr_result.as_bool());
			
			case Type::Char:
			case Type::Short:
			case Type::Int:
			case Type::Long:
			case Type::LongLong:
				return EvalResult::from_int(expr_result.as_int());
			
			case Type::UnsignedChar:
			case Type::UnsignedShort:
			case Type::UnsignedInt:
			case Type::UnsignedLong:
			case Type::UnsignedLongLong:
				// For unsigned types, convert to unsigned
				return EvalResult::from_uint(static_cast<unsigned long long>(expr_result.as_int()));
			
			case Type::Float:
			case Type::Double:
			case Type::LongDouble:
				return EvalResult::from_double(expr_result.as_double());
			
			default:
				return EvalResult::error("Unsupported type in static_cast for constant evaluation");
		}
	}

	static EvalResult evaluate_identifier(const IdentifierNode& identifier, EvaluationContext& context) {
		// Look up the identifier in the symbol table
		if (!context.symbols) {
			return EvalResult::error("Cannot evaluate variable reference: no symbol table provided");
		}

		std::string_view var_name = identifier.name();
		auto symbol_opt = context.symbols->lookup(var_name);
		if (!symbol_opt.has_value()) {
			return EvalResult::error("Undefined variable in constant expression: " + std::string(var_name));
		}

		const ASTNode& symbol_node = symbol_opt.value();
		
		// Check if it's a VariableDeclarationNode
		if (!symbol_node.is<VariableDeclarationNode>()) {
			return EvalResult::error("Identifier in constant expression is not a variable: " + std::string(var_name));
		}

		const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
		
		// Check if it's a constexpr variable
		if (!var_decl.is_constexpr()) {
			return EvalResult::error("Variable in constant expression must be constexpr: " + std::string(var_name));
		}

		// Get the initializer
		const auto& initializer = var_decl.initializer();
		if (!initializer.has_value()) {
			return EvalResult::error("Constexpr variable has no initializer: " + std::string(var_name));
		}

		// Recursively evaluate the initializer
		return evaluate(initializer.value(), context);
	}

	static EvalResult evaluate_ternary_operator(const TernaryOperatorNode& ternary, EvaluationContext& context) {
		// Evaluate the condition
		auto cond_result = evaluate(ternary.condition(), context);
		if (!cond_result.success) {
			return cond_result;
		}

		// Evaluate the appropriate branch based on the condition
		if (cond_result.as_bool()) {
			return evaluate(ternary.true_expr(), context);
		} else {
			return evaluate(ternary.false_expr(), context);
		}
	}

	static EvalResult evaluate_function_call(const FunctionCallNode& func_call, EvaluationContext& context) {
		// Check recursion depth
		if (context.current_depth >= context.max_recursion_depth) {
			return EvalResult::error("Constexpr recursion depth limit exceeded");
		}

		// Get the function declaration
		const DeclarationNode& func_decl_node = func_call.function_declaration();
		
		// Look up the function in the symbol table to get the FunctionDeclarationNode
		if (!context.symbols) {
			return EvalResult::error("Cannot evaluate function call: no symbol table provided");
		}

		std::string_view func_name = func_decl_node.identifier_token().value();
		auto symbol_opt = context.symbols->lookup(func_name);
		if (!symbol_opt.has_value()) {
			return EvalResult::error("Undefined function in constant expression: " + std::string(func_name));
		}

		const ASTNode& symbol_node = symbol_opt.value();
		
		// Check if it's a FunctionDeclarationNode
		if (!symbol_node.is<FunctionDeclarationNode>()) {
			return EvalResult::error("Identifier is not a function: " + std::string(func_name));
		}

		const FunctionDeclarationNode& func_decl = symbol_node.as<FunctionDeclarationNode>();
		
		// Check if it's a constexpr function
		if (!func_decl.is_constexpr()) {
			return EvalResult::error("Function in constant expression must be constexpr: " + std::string(func_name));
		}

		// Get the function body
		const auto& definition = func_decl.get_definition();
		if (!definition.has_value()) {
			return EvalResult::error("Constexpr function has no body: " + std::string(func_name));
		}

		// Evaluate arguments
		const auto& arguments = func_call.arguments();
		const auto& parameters = func_decl.parameter_nodes();
		
		if (arguments.size() != parameters.size()) {
			return EvalResult::error("Function argument count mismatch in constant expression");
		}

		// Pass empty bindings for top-level function calls
		std::unordered_map<std::string_view, EvalResult> empty_bindings;
		return evaluate_function_call_with_bindings(func_decl, arguments, empty_bindings, context);
	}

	static EvalResult evaluate_function_call_with_bindings(
		const FunctionDeclarationNode& func_decl,
		const ChunkedVector<ASTNode>& arguments,
		const std::unordered_map<std::string_view, EvalResult>& outer_bindings,
		EvaluationContext& context) {
		
		// Check recursion depth
		if (context.current_depth >= context.max_recursion_depth) {
			return EvalResult::error("Constexpr recursion depth limit exceeded");
		}

		// Get the function body
		const auto& definition = func_decl.get_definition();
		if (!definition.has_value()) {
			return EvalResult::error("Constexpr function has no body");
		}

		// Evaluate arguments
		const auto& parameters = func_decl.parameter_nodes();
		
		if (arguments.size() != parameters.size()) {
			return EvalResult::error("Function argument count mismatch in constant expression");
		}

		// Create a new symbol table scope for the function
		// We'll use a simple map to bind parameters to their evaluated values
		std::unordered_map<std::string_view, EvalResult> param_bindings;
		
		for (size_t i = 0; i < arguments.size(); ++i) {
			// Evaluate the argument with outer bindings (for nested calls)
			auto arg_result = evaluate_expression_with_bindings(arguments[i], outer_bindings, context);
			if (!arg_result.success) {
				return arg_result;
			}
			
			// Get parameter name
			const ASTNode& param_node = parameters[i];
			if (!param_node.is<DeclarationNode>()) {
				return EvalResult::error("Invalid parameter node");
			}
			
			const DeclarationNode& param_decl = param_node.as<DeclarationNode>();
			std::string_view param_name = param_decl.identifier_token().value();
			
			// Bind parameter to its value
			param_bindings[param_name] = arg_result;
		}

		// Increase recursion depth
		context.current_depth++;
		
		// Evaluate the function body with parameter bindings
		const ASTNode& body_node = definition.value();
		if (!body_node.is<BlockNode>()) {
			context.current_depth--;
			return EvalResult::error("Function body is not a block");
		}
		
		const BlockNode& body = body_node.as<BlockNode>();
		const auto& statements = body.get_statements();
		
		// For simple constexpr functions, we expect a single return statement
		if (statements.size() != 1) {
			context.current_depth--;
			return EvalResult::error("Constexpr function must have a single return statement (complex statements not yet supported)");
		}
		
		auto result = evaluate_statement_with_bindings(statements[0], param_bindings, context);
		context.current_depth--;
		return result;
	}

	static EvalResult evaluate_statement_with_bindings(
		const ASTNode& stmt_node,
		const std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context) {
		
		// Check if it's a return statement
		if (stmt_node.is<ReturnStatementNode>()) {
			const ReturnStatementNode& ret_stmt = stmt_node.as<ReturnStatementNode>();
			const auto& return_expr = ret_stmt.expression();
			
			if (!return_expr.has_value()) {
				return EvalResult::error("Constexpr function return statement has no expression");
			}
			
			return evaluate_expression_with_bindings(return_expr.value(), bindings, context);
		}
		
		return EvalResult::error("Unsupported statement type in constexpr function");
	}

	static EvalResult evaluate_expression_with_bindings(
		const ASTNode& expr_node,
		const std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context) {
		
		if (!expr_node.is<ExpressionNode>()) {
			return EvalResult::error("Not an expression node");
		}
		
		const ExpressionNode& expr = expr_node.as<ExpressionNode>();
		
		// Check if it's an identifier that matches a parameter
		if (std::holds_alternative<IdentifierNode>(expr)) {
			const IdentifierNode& id = std::get<IdentifierNode>(expr);
			std::string_view name = id.name();
			
			// Check if it's a bound parameter
			auto it = bindings.find(name);
			if (it != bindings.end()) {
				return it->second;  // Return the bound value
			}
			
			// Not a parameter, evaluate normally
			return evaluate_identifier(id, context);
		}
		
		// For binary operators, recursively evaluate with bindings
		if (std::holds_alternative<BinaryOperatorNode>(expr)) {
			const auto& bin_op = std::get<BinaryOperatorNode>(expr);
			auto lhs_result = evaluate_expression_with_bindings(bin_op.get_lhs(), bindings, context);
			auto rhs_result = evaluate_expression_with_bindings(bin_op.get_rhs(), bindings, context);
			
			if (!lhs_result.success) return lhs_result;
			if (!rhs_result.success) return rhs_result;
			
			return apply_binary_op(lhs_result, rhs_result, bin_op.op());
		}
		
		// For ternary operators
		if (std::holds_alternative<TernaryOperatorNode>(expr)) {
			const auto& ternary = std::get<TernaryOperatorNode>(expr);
			auto cond_result = evaluate_expression_with_bindings(ternary.condition(), bindings, context);
			
			if (!cond_result.success) return cond_result;
			
			if (cond_result.as_bool()) {
				return evaluate_expression_with_bindings(ternary.true_expr(), bindings, context);
			} else {
				return evaluate_expression_with_bindings(ternary.false_expr(), bindings, context);
			}
		}
		
		// For function calls (for recursion)
		if (std::holds_alternative<FunctionCallNode>(expr)) {
			const auto& func_call = std::get<FunctionCallNode>(expr);
			
			// Look up the function
			const DeclarationNode& func_decl_node = func_call.function_declaration();
			std::string_view func_name = func_decl_node.identifier_token().value();
			
			if (!context.symbols) {
				return EvalResult::error("Cannot evaluate function call: no symbol table provided");
			}
			
			auto symbol_opt = context.symbols->lookup(func_name);
			if (!symbol_opt.has_value()) {
				return EvalResult::error("Undefined function in constant expression: " + std::string(func_name));
			}
			
			const ASTNode& symbol_node = symbol_opt.value();
			if (!symbol_node.is<FunctionDeclarationNode>()) {
				return EvalResult::error("Identifier is not a function: " + std::string(func_name));
			}
			
			const FunctionDeclarationNode& func_decl = symbol_node.as<FunctionDeclarationNode>();
			
			// Check if it's a constexpr function
			if (!func_decl.is_constexpr()) {
				return EvalResult::error("Function in constant expression must be constexpr: " + std::string(func_name));
			}
			
			// Evaluate the function with bindings passed through
			return evaluate_function_call_with_bindings(func_decl, func_call.arguments(), bindings, context);
		}
		
		// For literals and other expressions without parameters, evaluate normally
		return evaluate(expr_node, context);
	}

	// Helper functions for overflow-safe arithmetic using compiler builtins
private:
	// Perform addition with overflow checking, return result or nullopt on overflow
	static std::optional<long long> safe_add(long long a, long long b) {
		long long result;
#if defined(_MSC_VER) && !defined(__clang__)
		// MSVC implementation using manual overflow detection
		if ((b > 0 && a > LLONG_MAX - b) || (b < 0 && a < LLONG_MIN - b)) {
			return std::nullopt; // Overflow
		}
		result = a + b;
		bool overflow = false;
#else
		bool overflow = __builtin_add_overflow(a, b, &result);
#endif
		return overflow ? std::nullopt : std::optional<long long>(result);
	}

	// Perform subtraction with overflow checking, return result or nullopt on overflow
	static std::optional<long long> safe_sub(long long a, long long b) {
		long long result;
#if defined(_MSC_VER) && !defined(__clang__)
		// MSVC implementation using manual overflow detection
		if ((b < 0 && a > LLONG_MAX + b) || (b > 0 && a < LLONG_MIN + b)) {
			return std::nullopt; // Overflow
		}
		result = a - b;
		bool overflow = false;
#else
		bool overflow = __builtin_sub_overflow(a, b, &result);
#endif
		return overflow ? std::nullopt : std::optional<long long>(result);
	}

	// Perform multiplication with overflow checking, return result or nullopt on overflow
	static std::optional<long long> safe_mul(long long a, long long b) {
		long long result;
#if defined(_MSC_VER) && !defined(__clang__)
		// MSVC implementation using manual overflow detection
		if (a == 0 || b == 0) {
			result = 0;
		} else if (a == LLONG_MIN || b == LLONG_MIN) {
			// Special case: LLONG_MIN * anything except 0 or 1 overflows
			if ((a == LLONG_MIN && (b < -1 || b > 1)) || (b == LLONG_MIN && (a < -1 || a > 1))) {
				return std::nullopt;
			}
			result = a * b;
		} else if ((a > 0 && b > 0 && a > LLONG_MAX / b) ||
		           (a > 0 && b < 0 && b < LLONG_MIN / a) ||
		           (a < 0 && b > 0 && a < LLONG_MIN / b) ||
		           (a < 0 && b < 0 && a < LLONG_MAX / b)) {
			return std::nullopt; // Overflow
		} else {
			result = a * b;
		}
		bool overflow = false;
#else
		bool overflow = __builtin_mul_overflow(a, b, &result);
#endif
		return overflow ? std::nullopt : std::optional<long long>(result);
	}

	// Perform left shift with validation and overflow checking, return result or nullopt on error
	static std::optional<long long> safe_shl(long long a, long long b) {
		if (b < 0 || b >= 64) {
			return std::nullopt; // Negative shift or shift >= bit width is undefined
		}
		if (a == 0) {
			return 0; // Shifting zero is fine
		}
		
		// Check if the shift would cause bits to be lost
		// For left shift, check if any bits would be shifted out
		long long shifted = a << b;
		long long back_shifted = shifted >> b;
		if (back_shifted != a) {
			return std::nullopt; // Overflow detected
		}
		
		return shifted;
	}

	// Perform right shift with validation, return result or nullopt on error
	static std::optional<long long> safe_shr(long long a, long long b) {
		if (b < 0 || b >= 64) {
			return std::nullopt; // Negative shift or shift >= bit width is undefined
		}
		return a >> b; // Right shift never overflows mathematically
	}

public:
	// Helper to apply binary operators
	static EvalResult apply_binary_op(const EvalResult& lhs, const EvalResult& rhs, std::string_view op) {
		long long lhs_val = lhs.as_int();
		long long rhs_val = rhs.as_int();
		
		// Handle arithmetic operators with overflow checking
		if (op == "+") {
			if (auto result = safe_add(lhs_val, rhs_val)) {
				return EvalResult::from_int(*result);
			} else {
				return EvalResult::error("Signed integer overflow in constant expression");
			}
		} else if (op == "-") {
			if (auto result = safe_sub(lhs_val, rhs_val)) {
				return EvalResult::from_int(*result);
			} else {
				return EvalResult::error("Signed integer overflow in constant expression");
			}
		} else if (op == "*") {
			if (auto result = safe_mul(lhs_val, rhs_val)) {
				return EvalResult::from_int(*result);
			} else {
				return EvalResult::error("Signed integer overflow in constant expression");
			}
		} else if (op == "/") {
			if (rhs_val == 0) {
				return EvalResult::error("Division by zero in constant expression");
			}
			// Check for overflow in division (only happens with LLONG_MIN / -1)
			if (lhs_val == LLONG_MIN && rhs_val == -1) {
				return EvalResult::error("Signed integer overflow in constant expression");
			}
			return EvalResult::from_int(lhs_val / rhs_val);
		} else if (op == "%") {
			if (rhs_val == 0) {
				return EvalResult::error("Modulo by zero in constant expression");
			}
			return EvalResult::from_int(lhs_val % rhs_val);
		}
		
		// Handle bitwise operators
		else if (op == "&") {
			return EvalResult::from_int(lhs_val & rhs_val);
		} else if (op == "|") {
			return EvalResult::from_int(lhs_val | rhs_val);
		} else if (op == "^") {
			return EvalResult::from_int(lhs_val ^ rhs_val);
		} else if (op == "<<") {
			if (auto result = safe_shl(lhs_val, rhs_val)) {
				return EvalResult::from_int(*result);
			} else {
				return EvalResult::error("Left shift overflow or invalid shift count in constant expression");
			}
		} else if (op == ">>") {
			if (auto result = safe_shr(lhs_val, rhs_val)) {
				return EvalResult::from_int(*result);
			} else {
				return EvalResult::error("Invalid shift count in constant expression");
			}
		}
		
		// Handle comparison operators that work on integers
		if (op == "==") {
			// Compare as integers for all types
			return EvalResult::from_bool(lhs.as_int() == rhs.as_int());
		} else if (op == "!=") {
			return EvalResult::from_bool(lhs.as_int() != rhs.as_int());
		} else if (op == "<") {
			return EvalResult::from_bool(lhs.as_int() < rhs.as_int());
		} else if (op == "<=") {
			return EvalResult::from_bool(lhs.as_int() <= rhs.as_int());
		} else if (op == ">") {
			return EvalResult::from_bool(lhs.as_int() > rhs.as_int());
		} else if (op == ">=") {
			return EvalResult::from_bool(lhs.as_int() >= rhs.as_int());
		} else if (op == "&&") {
			return EvalResult::from_bool(lhs.as_bool() && rhs.as_bool());
		} else if (op == "||") {
			return EvalResult::from_bool(lhs.as_bool() || rhs.as_bool());
		}

		// Unsupported operator
		return EvalResult::error("Operator '" + std::string(op) + "' not supported in constant expressions");
	}

	static EvalResult apply_unary_op(const EvalResult& operand, std::string_view op) {
		if (op == "!") {
			return EvalResult::from_bool(!operand.as_bool());
		} else if (op == "~") {
			return EvalResult::from_int(~operand.as_int());
		}

		// Unsupported operator
		return EvalResult::error("Unary operator '" + std::string(op) + "' not supported in constant expressions");
	}

	// Evaluate qualified identifier (e.g., Namespace::var or Template<T>::member)
	static EvalResult evaluate_qualified_identifier(const QualifiedIdentifierNode& qualified_id, EvaluationContext& context) {
		// Look up the qualified name in the symbol table
		if (!context.symbols) {
			return EvalResult::error("Cannot evaluate qualified identifier: no symbol table provided");
		}

		// Try to look up the qualified name
		auto symbol_opt = context.symbols->lookup_qualified(qualified_id.namespaces(), qualified_id.name());
		if (!symbol_opt.has_value()) {
			// For now, return error - in the future we may need to handle template instantiation
			return EvalResult::error("Undefined qualified identifier in constant expression: " + qualified_id.full_name());
		}

		const ASTNode& symbol_node = *symbol_opt;

		// Check if it's a variable declaration (constexpr)
		if (symbol_node.is<VariableDeclarationNode>()) {
			const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
			if (!var_decl.is_constexpr()) {
				return EvalResult::error("Qualified variable must be constexpr: " + qualified_id.full_name());
			}
			const auto& initializer = var_decl.initializer();
			if (!initializer.has_value()) {
				return EvalResult::error("Constexpr variable has no initializer: " + qualified_id.full_name());
			}
			return evaluate(initializer.value(), context);
		}

		// Could be other types like enum constants - add support as needed
		return EvalResult::error("Qualified identifier is not a constant expression: " + qualified_id.full_name());
	}

	// Evaluate member access (e.g., obj.member or struct_type::static_member)
	static EvalResult evaluate_member_access(const MemberAccessNode& member_access, EvaluationContext& context) {
		// Get the object expression (e.g., 'p1' in 'p1.x')
		const ASTNode& object_expr = member_access.object();
		std::string_view member_name = member_access.member_name();
		
		// For constexpr struct member access, we need to handle the case where:
		// - The object is an identifier referencing a constexpr variable
		// - The variable is initialized with a ConstructorCallNode
		// - We need to find the constructor declaration and its member initializer list
		// - Extract the member value from the initializer expression
		
		// The object might be wrapped in an ExpressionNode, so unwrap it
		// Extract the identifier name
		std::string_view var_name;
		
		if (object_expr.is<ExpressionNode>()) {
			const ExpressionNode& expr_node = object_expr.as<ExpressionNode>();
			// The ExpressionNode uses std::variant, check if it contains an IdentifierNode
			if (!std::holds_alternative<IdentifierNode>(expr_node)) {
				return EvalResult::error("Complex member access expressions not yet supported in constant expressions");
			}
			const IdentifierNode& id_node = std::get<IdentifierNode>(expr_node);
			var_name = id_node.name();
		} else if (object_expr.is<IdentifierNode>()) {
			const IdentifierNode& id_node = object_expr.as<IdentifierNode>();
			var_name = id_node.name();
		} else {
			return EvalResult::error("Complex member access expressions not yet supported in constant expressions");
		}
		
		// Look up the variable in the symbol table
		if (!context.symbols) {
			return EvalResult::error("Cannot evaluate member access: no symbol table provided");
		}
		
		auto symbol_opt = context.symbols->lookup(var_name);
		if (!symbol_opt.has_value()) {
			return EvalResult::error("Undefined variable in member access: " + std::string(var_name));
		}
		
		const ASTNode& symbol_node = symbol_opt.value();
		
		// Check if it's a VariableDeclarationNode
		if (!symbol_node.is<VariableDeclarationNode>()) {
			return EvalResult::error("Identifier in member access is not a variable: " + std::string(var_name));
		}
		
		const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
		
		// Check if it's a constexpr variable
		if (!var_decl.is_constexpr()) {
			return EvalResult::error("Variable in member access must be constexpr: " + std::string(var_name));
		}
		
		// Get the initializer
		const auto& initializer = var_decl.initializer();
		if (!initializer.has_value()) {
			return EvalResult::error("Constexpr variable has no initializer: " + std::string(var_name));
		}
		
		// Check if the initializer is a ConstructorCallNode
		if (!initializer->is<ConstructorCallNode>()) {
			return EvalResult::error("Member access on non-struct constexpr variable not supported");
		}
		
		const ConstructorCallNode& ctor_call = initializer->as<ConstructorCallNode>();
		
		// Get the type being constructed
		const ASTNode& type_node = ctor_call.type_node();
		if (!type_node.is<TypeSpecifierNode>()) {
			return EvalResult::error("Constructor call without valid type specifier");
		}
		
		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
		
		// Get the struct type info
		if (type_spec.type() != Type::Struct && type_spec.type() != Type::UserDefined) {
			return EvalResult::error("Member access requires a struct type");
		}
		
		TypeIndex type_index = type_spec.type_index();
		if (type_index >= gTypeInfo.size()) {
			return EvalResult::error("Invalid type index in member access");
		}
		
		const TypeInfo& struct_type_info = gTypeInfo[type_index];
		const StructTypeInfo* struct_info = struct_type_info.getStructInfo();
		if (!struct_info) {
			return EvalResult::error("Type is not a struct in member access");
		}
		
		// Get the constructor arguments from the call
		const auto& ctor_args = ctor_call.arguments();
		
		// Find the matching constructor in the struct
		// We need to find a constructor with the same number of parameters as arguments
		const ConstructorDeclarationNode* matching_ctor = nullptr;
		for (const auto& member_func : struct_info->member_functions) {
			if (!member_func.is_constructor) {
				continue;
			}
			if (!member_func.function_decl.is<ConstructorDeclarationNode>()) {
				continue;
			}
			const ConstructorDeclarationNode& ctor = member_func.function_decl.as<ConstructorDeclarationNode>();
			if (ctor.parameter_nodes().size() == ctor_args.size()) {
				// Found a constructor with matching parameter count
				// For full correctness, we should check parameter types too, but for constexpr
				// evaluation in simple cases, parameter count matching is sufficient
				matching_ctor = &ctor;
				break;
			}
		}
		
		if (!matching_ctor) {
			return EvalResult::error("No matching constructor found for constexpr evaluation");
		}
		
		// Look for the member in the constructor's member initializer list
		const auto& member_inits = matching_ctor->member_initializers();
		for (const auto& mem_init : member_inits) {
			if (mem_init.member_name == member_name) {
				// Found the member initializer
				// Create a context with parameter bindings
				// We need to substitute constructor parameters with the actual arguments
				
				// For now, implement a simple case: direct assignment from parameter
				// e.g., Point(int x_val, int y_val) : x(x_val), y(y_val) {}
				
				const ASTNode& init_expr = mem_init.initializer_expr;
				
				// The initializer might be wrapped in an ExpressionNode
				bool is_simple_identifier = false;
				std::string_view param_name;
				
				if (init_expr.is<ExpressionNode>()) {
					const ExpressionNode& expr_node = init_expr.as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(expr_node)) {
						const IdentifierNode& param_id = std::get<IdentifierNode>(expr_node);
						param_name = param_id.name();
						is_simple_identifier = true;
					}
				} else if (init_expr.is<IdentifierNode>()) {
					const IdentifierNode& param_id = init_expr.as<IdentifierNode>();
					param_name = param_id.name();
					is_simple_identifier = true;
				}
				
				if (is_simple_identifier) {
					// Find which parameter this refers to
					const auto& params = matching_ctor->parameter_nodes();
					for (size_t i = 0; i < params.size(); ++i) {
						if (params[i].is<DeclarationNode>()) {
							const DeclarationNode& param_decl = params[i].as<DeclarationNode>();
							if (param_decl.identifier_token().value() == param_name) {
								// Found the parameter - evaluate the corresponding argument
								if (i < ctor_args.size()) {
									return evaluate(ctor_args[i], context);
								}
							}
						}
					}
				}
				
				// For more complex initializer expressions, we would need to:
				// 1. Build a parameter substitution map
				// 2. Recursively evaluate the initializer with parameter substitution
				// For now, return an error for complex cases
				return EvalResult::error("Complex member initializer expressions not yet supported in constexpr evaluation");
			}
		}
		
		// Member not found in initializer list - it might have a default member initializer
		// or be value-initialized (0 for integers, etc.)
		return EvalResult::error("Member '" + std::string(member_name) + "' not found in constructor initializer list");
	}
};

} // namespace ConstExpr
