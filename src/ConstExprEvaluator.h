#pragma once

#include "AstNodeTypes.h"
#include <optional>
#include <string>
#include <variant>
#include <unordered_map>

// Forward declarations
class SymbolTable;
struct TypeInfo;

namespace ConstExpr {

// Representation of a struct/class object in constant evaluation
struct ObjectValue {
	std::string type_name;  // Name of the struct/class type
	std::unordered_map<std::string, std::variant<
		bool,
		long long,
		unsigned long long,
		double
	>> members;  // Member name -> value mapping
	
	ObjectValue() = default;
	ObjectValue(std::string name) : type_name(std::move(name)) {}
};

// Result of constant expression evaluation
struct EvalResult {
	bool success;
	std::variant<
		bool,                    // Boolean constant
		long long,               // Signed integer constant
		unsigned long long,      // Unsigned integer constant
		double,                  // Floating-point constant
		ObjectValue              // Struct/class instance
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

	static EvalResult from_object(ObjectValue obj) {
		return EvalResult{true, std::move(obj), ""};
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

		// For MemberAccessNode (e.g., obj.member)
		if (std::holds_alternative<MemberAccessNode>(expr)) {
			return evaluate_member_access(std::get<MemberAccessNode>(expr), context);
		}

		// For TernaryOperatorNode (condition ? true_expr : false_expr)
		if (std::holds_alternative<TernaryOperatorNode>(expr)) {
			return evaluate_ternary_operator(std::get<TernaryOperatorNode>(expr), context);
		}

		// For FunctionCallNode (constexpr function calls)
		if (std::holds_alternative<FunctionCallNode>(expr)) {
			return evaluate_function_call(std::get<FunctionCallNode>(expr), context);
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

	static EvalResult evaluate_member_access(const MemberAccessNode& member_access, EvaluationContext& context) {
		// This is for evaluating expressions like obj.member
		// We need to evaluate the object first, then access its member
		
		// For now, we'll only support accessing members of objects stored in bindings
		// The object must be an identifier that resolves to an ObjectValue
		
		const ASTNode& object_node = member_access.object();
		if (!object_node.is<ExpressionNode>()) {
			return EvalResult::error("Member access object is not an expression");
		}
		
		const ExpressionNode& object_expr = object_node.as<ExpressionNode>();
		
		// Check if it's an identifier
		if (!std::holds_alternative<IdentifierNode>(object_expr)) {
			return EvalResult::error("Member access currently only supported on identifiers in constant expressions");
		}
		
		const IdentifierNode& object_id = std::get<IdentifierNode>(object_expr);
		std::string_view object_name = object_id.name();
		
		// Look up the object in the symbol table's runtime bindings
		// Note: We can't do this with the current design that only has static constexpr variables
		// We need to extend this to support function-local objects
		
		return EvalResult::error("Member access on objects not yet fully implemented in constant expressions");
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
		
		// Check if it's a constexpr or consteval function
		if (!func_decl.is_constexpr() && !func_decl.is_consteval()) {
			return EvalResult::error("Function in constant expression must be constexpr or consteval: " + std::string(func_name));
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
		
		// Evaluate all statements in the function body
		// Support multiple statements for C++14/C++20 constexpr functions with loops and local variables
		auto result = evaluate_block_with_bindings(statements, param_bindings, context);
		context.current_depth--;
		return result;
	}

	// Evaluate a block of statements with mutable bindings
	// Returns the result of a return statement, or error if no return
	static EvalResult evaluate_block_with_bindings(
		const ChunkedVector<ASTNode, 128, 256>& statements,
		std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context) {
		
		for (size_t i = 0; i < statements.size(); ++i) {
			const ASTNode& stmt = statements[i];
			
			// Check if it's a return statement before evaluating
			if (stmt.is<ReturnStatementNode>()) {
				auto result = evaluate_statement_with_bindings_mutable(stmt, bindings, context);
				// Return statements always return the value, regardless of type
				return result;
			}
			
			// For non-return statements, evaluate and continue
			auto result = evaluate_statement_with_bindings_mutable(stmt, bindings, context);
			if (!result.success) {
				return result;
			}
			// Otherwise continue to next statement
		}
		
		return EvalResult::error("Constexpr function reached end without return statement");
	}

	// Evaluate a single statement with mutable bindings
	// This allows variable declarations and assignments
	static EvalResult evaluate_statement_with_bindings_mutable(
		const ASTNode& stmt_node,
		std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context) {
		
		// Check if it's a return statement
		if (stmt_node.is<ReturnStatementNode>()) {
			const ReturnStatementNode& ret_stmt = stmt_node.as<ReturnStatementNode>();
			const auto& return_expr = ret_stmt.expression();
			
			if (!return_expr.has_value()) {
				return EvalResult::error("Constexpr function return statement has no expression");
			}
			
			return evaluate_expression_with_bindings_mutable(return_expr.value(), bindings, context);
		}
		
		// Check if it's a variable declaration
		if (stmt_node.is<VariableDeclarationNode>()) {
			const VariableDeclarationNode& var_decl = stmt_node.as<VariableDeclarationNode>();
			const DeclarationNode& decl = var_decl.declaration();
			
			std::string_view var_name = decl.identifier_token().value();
			
			// Evaluate the initializer if present
			const auto& initializer = var_decl.initializer();
			if (initializer.has_value()) {
				auto init_result = evaluate_expression_with_bindings_mutable(initializer.value(), bindings, context);
				if (!init_result.success) {
					return init_result;
				}
				bindings[var_name] = init_result;
			} else {
				// Default initialize to 0
				bindings[var_name] = EvalResult::from_int(0);
			}
			
			// Variable declarations don't return a value, return success marker
			return EvalResult::from_int(0);
		}
		
		// Check if it's a for loop
		if (stmt_node.is<ForStatementNode>()) {
			return evaluate_for_loop(stmt_node.as<ForStatementNode>(), bindings, context);
		}
		
		// Check if it's a while loop
		if (stmt_node.is<WhileStatementNode>()) {
			return evaluate_while_loop(stmt_node.as<WhileStatementNode>(), bindings, context);
		}
		
		// Check if it's an expression statement (e.g., assignment)
		if (stmt_node.is<ExpressionNode>()) {
			auto result = evaluate_expression_with_bindings_mutable(stmt_node, bindings, context);
			// Expression statements don't return a value for the function
			return EvalResult::from_int(0);
		}
		
		// Check if it's an if statement
		if (stmt_node.is<IfStatementNode>()) {
			return evaluate_if_statement(stmt_node.as<IfStatementNode>(), bindings, context);
		}
		
		return EvalResult::error("Unsupported statement type in constexpr function");
	}

	// Evaluate a for loop
	static EvalResult evaluate_for_loop(
		const ForStatementNode& for_loop,
		std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context) {
		
		// Execute init statement if present
		if (for_loop.has_init()) {
			auto init_result = evaluate_statement_with_bindings_mutable(for_loop.get_init_statement().value(), bindings, context);
			if (!init_result.success) {
				return init_result;
			}
		}
		
		// Loop until condition is false
		while (true) {
			// Check condition if present
			if (for_loop.has_condition()) {
				auto cond_result = evaluate_expression_with_bindings_mutable(for_loop.get_condition().value(), bindings, context);
				if (!cond_result.success) {
					return cond_result;
				}
				if (!cond_result.as_bool()) {
					break;  // Condition is false, exit loop
				}
			}
			
			// Execute body
			const ASTNode& body = for_loop.get_body_statement();
			if (body.is<BlockNode>()) {
				const BlockNode& block = body.as<BlockNode>();
				const auto& statements = block.get_statements();
				for (size_t i = 0; i < statements.size(); ++i) {
					const ASTNode& stmt = statements[i];
					auto result = evaluate_statement_with_bindings_mutable(stmt, bindings, context);
					if (!result.success) {
						return result;
					}
					// If it's a return statement, propagate it up
					if (stmt.is<ReturnStatementNode>()) {
						return result;
					}
				}
			} else {
				auto result = evaluate_statement_with_bindings_mutable(body, bindings, context);
				if (!result.success) {
					return result;
				}
				// If it's a return statement, propagate it up
				if (body.is<ReturnStatementNode>()) {
					return result;
				}
			}
			
			// Execute update expression if present
			if (for_loop.has_update()) {
				auto update_result = evaluate_expression_with_bindings_mutable(for_loop.get_update_expression().value(), bindings, context);
				if (!update_result.success) {
					return update_result;
				}
			}
			
			// Safety check to prevent infinite loops
			if (++context.step_count > context.max_steps) {
				return EvalResult::error("Constexpr evaluation exceeded complexity limit (infinite loop?)");
			}
		}
		
		// For loops don't return a value
		return EvalResult::from_int(0);
	}

	// Evaluate a while loop
	static EvalResult evaluate_while_loop(
		const WhileStatementNode& while_loop,
		std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context) {
		
		while (true) {
			// Evaluate condition
			auto cond_result = evaluate_expression_with_bindings_mutable(while_loop.get_condition(), bindings, context);
			if (!cond_result.success) {
				return cond_result;
			}
			if (!cond_result.as_bool()) {
				break;  // Condition is false, exit loop
			}
			
			// Execute body
			const ASTNode& body = while_loop.get_body_statement();
			if (body.is<BlockNode>()) {
				const BlockNode& block = body.as<BlockNode>();
				const auto& statements = block.get_statements();
				for (size_t i = 0; i < statements.size(); ++i) {
					const ASTNode& stmt = statements[i];
					auto result = evaluate_statement_with_bindings_mutable(stmt, bindings, context);
					if (!result.success) {
						return result;
					}
					// If it's a return statement, propagate it up
					if (stmt.is<ReturnStatementNode>()) {
						return result;
					}
				}
			} else {
				auto result = evaluate_statement_with_bindings_mutable(body, bindings, context);
				if (!result.success) {
					return result;
				}
				// If it's a return statement, propagate it up
				if (body.is<ReturnStatementNode>()) {
					return result;
				}
			}
			
			// Safety check to prevent infinite loops
			if (++context.step_count > context.max_steps) {
				return EvalResult::error("Constexpr evaluation exceeded complexity limit (infinite loop?)");
			}
		}
		
		// While loops don't return a value
		return EvalResult::from_int(0);
	}

	// Evaluate an if statement  
	static EvalResult evaluate_if_statement(
		const IfStatementNode& if_stmt,
		std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context) {
		
		// Evaluate condition
		auto cond_result = evaluate_expression_with_bindings_mutable(if_stmt.get_condition(), bindings, context);
		if (!cond_result.success) {
			return cond_result;
		}
		
		// Execute appropriate branch
		if (cond_result.as_bool()) {
			// Execute then branch
			const ASTNode& branch = if_stmt.get_then_statement();
			if (branch.is<BlockNode>()) {
				const BlockNode& block = branch.as<BlockNode>();
				const auto& statements = block.get_statements();
				for (size_t i = 0; i < statements.size(); ++i) {
					const ASTNode& stmt = statements[i];
					auto result = evaluate_statement_with_bindings_mutable(stmt, bindings, context);
					if (!result.success) {
						return result;
					}
					// If it's a return statement, propagate it up
					if (stmt.is<ReturnStatementNode>()) {
						return result;
					}
				}
			} else {
				auto result = evaluate_statement_with_bindings_mutable(branch, bindings, context);
				if (!result.success) {
					return result;
				}
				// If it's a return statement, propagate it up
				if (branch.is<ReturnStatementNode>()) {
					return result;
				}
			}
		} else if (if_stmt.has_else()) {
			// Execute else branch
			const std::optional<ASTNode>& else_opt = if_stmt.get_else_statement();
			const ASTNode& branch = else_opt.value();
			if (branch.is<BlockNode>()) {
				const BlockNode& block = branch.as<BlockNode>();
				const auto& statements = block.get_statements();
				for (size_t i = 0; i < statements.size(); ++i) {
					const ASTNode& stmt = statements[i];
					auto result = evaluate_statement_with_bindings_mutable(stmt, bindings, context);
					if (!result.success) {
						return result;
					}
					// If it's a return statement, propagate it up
					if (stmt.is<ReturnStatementNode>()) {
						return result;
					}
				}
			} else {
				auto result = evaluate_statement_with_bindings_mutable(branch, bindings, context);
				if (!result.success) {
					return result;
				}
				// If it's a return statement, propagate it up
				if (branch.is<ReturnStatementNode>()) {
					return result;
				}
			}
		}
		// No else branch and condition was false, or branches completed without return
		
		// If statements don't return a value
		return EvalResult::from_int(0);
	}

	static EvalResult evaluate_statement_with_bindings(
		const ASTNode& stmt_node,
		const std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context) {
		
		// For backwards compatibility, create mutable copy and delegate
		// This is only used for evaluating statements in contexts where we don't care about mutations
		std::unordered_map<std::string_view, EvalResult> mutable_bindings = bindings;
		return evaluate_statement_with_bindings_mutable(stmt_node, mutable_bindings, context);
	}

	static EvalResult evaluate_expression_with_bindings(
		const ASTNode& expr_node,
		const std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context) {
		
		// This creates a mutable copy to support potential assignments within the expression
		// Used primarily for evaluating function arguments where outer scope shouldn't be mutated
		// Any mutations to the copy are discarded after evaluation
		std::unordered_map<std::string_view, EvalResult> mutable_bindings = bindings;
		return evaluate_expression_with_bindings_mutable(expr_node, mutable_bindings, context);
	}

	static EvalResult evaluate_expression_with_bindings_mutable(
		const ASTNode& expr_node,
		std::unordered_map<std::string_view, EvalResult>& bindings,
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
		
		// For binary operators, check for assignment first
		if (std::holds_alternative<BinaryOperatorNode>(expr)) {
			const auto& bin_op = std::get<BinaryOperatorNode>(expr);
			std::string_view op = bin_op.op();
			
			// Handle assignment operators (=, +=, -=, etc.)
			if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=" ||
			    op == "&=" || op == "|=" || op == "^=" || op == "<<=" || op == ">>=") {
				
				// LHS must be an identifier
				const ASTNode& lhs = bin_op.get_lhs();
				if (!lhs.is<ExpressionNode>()) {
					return EvalResult::error("Assignment LHS is not an expression");
				}
				
				const ExpressionNode& lhs_expr = lhs.as<ExpressionNode>();
				if (!std::holds_alternative<IdentifierNode>(lhs_expr)) {
					return EvalResult::error("Assignment LHS must be an identifier");
				}
				
				const IdentifierNode& id = std::get<IdentifierNode>(lhs_expr);
				std::string_view var_name = id.name();
				
				// Evaluate RHS
				auto rhs_result = evaluate_expression_with_bindings_mutable(bin_op.get_rhs(), bindings, context);
				if (!rhs_result.success) {
					return rhs_result;
				}
				
				// Perform the assignment
				if (op == "=") {
					bindings[var_name] = rhs_result;
					return rhs_result;
				} else {
					// Compound assignment - get current value
					auto it = bindings.find(var_name);
					if (it == bindings.end()) {
						return EvalResult::error("Undefined variable in compound assignment: " + std::string(var_name));
					}
					
					EvalResult lhs_val = it->second;
					
					// Extract the base operator (e.g., "+=" -> "+")
					std::string base_op = std::string(op.substr(0, op.length() - 1));
					
					// Apply the operation
					auto result = apply_binary_op(lhs_val, rhs_result, base_op);
					if (!result.success) {
						return result;
					}
					
					bindings[var_name] = result;
					return result;
				}
			}
			
			// Regular binary operators
			auto lhs_result = evaluate_expression_with_bindings_mutable(bin_op.get_lhs(), bindings, context);
			auto rhs_result = evaluate_expression_with_bindings_mutable(bin_op.get_rhs(), bindings, context);
			
			if (!lhs_result.success) return lhs_result;
			if (!rhs_result.success) return rhs_result;
			
			return apply_binary_op(lhs_result, rhs_result, bin_op.op());
		}
		
		// Handle unary operators (including ++ and --)
		if (std::holds_alternative<UnaryOperatorNode>(expr)) {
			const auto& unary_op = std::get<UnaryOperatorNode>(expr);
			std::string_view op = unary_op.op();
			
			// Handle increment/decrement
			if (op == "++" || op == "--") {
				// Operand must be an identifier
				const ASTNode& operand = unary_op.get_operand();
				if (!operand.is<ExpressionNode>()) {
					return EvalResult::error("Increment/decrement operand is not an expression");
				}
				
				const ExpressionNode& operand_expr = operand.as<ExpressionNode>();
				if (!std::holds_alternative<IdentifierNode>(operand_expr)) {
					return EvalResult::error("Increment/decrement operand must be an identifier");
				}
				
				const IdentifierNode& id = std::get<IdentifierNode>(operand_expr);
				std::string_view var_name = id.name();
				
				// Get current value
				auto it = bindings.find(var_name);
				if (it == bindings.end()) {
					return EvalResult::error("Undefined variable in increment/decrement: " + std::string(var_name));
				}
				
				EvalResult old_val = it->second;
				long long int_val = old_val.as_int();
				
				// Compute new value
				long long new_val = (op == "++") ? (int_val + 1) : (int_val - 1);
				EvalResult new_result = EvalResult::from_int(new_val);
				
				// Update variable
				bindings[var_name] = new_result;
				
				// Return appropriate value based on prefix/postfix
				return unary_op.is_prefix() ? new_result : old_val;
			}
			
			// Regular unary operators
			auto operand_result = evaluate_expression_with_bindings_mutable(unary_op.get_operand(), bindings, context);
			if (!operand_result.success) {
				return operand_result;
			}
			return apply_unary_op(operand_result, op);
		}
		
		// For ternary operators
		if (std::holds_alternative<TernaryOperatorNode>(expr)) {
			const auto& ternary = std::get<TernaryOperatorNode>(expr);
			auto cond_result = evaluate_expression_with_bindings_mutable(ternary.condition(), bindings, context);
			
			if (!cond_result.success) return cond_result;
			
			if (cond_result.as_bool()) {
				return evaluate_expression_with_bindings_mutable(ternary.true_expr(), bindings, context);
			} else {
				return evaluate_expression_with_bindings_mutable(ternary.false_expr(), bindings, context);
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
			
			// Check if it's a constexpr or consteval function
			if (!func_decl.is_constexpr() && !func_decl.is_consteval()) {
				return EvalResult::error("Function in constant expression must be constexpr or consteval: " + std::string(func_name));
			}
			
			// Evaluate the function with bindings passed through
			return evaluate_function_call_with_bindings(func_decl, func_call.arguments(), bindings, context);
		}
		
		// For member access (e.g., obj.member)
		if (std::holds_alternative<MemberAccessNode>(expr)) {
			const auto& member_access = std::get<MemberAccessNode>(expr);
			
			// Evaluate the object expression
			const ASTNode& object_node = member_access.object();
			if (!object_node.is<ExpressionNode>()) {
				return EvalResult::error("Member access object is not an expression");
			}
			
			const ExpressionNode& object_expr = object_node.as<ExpressionNode>();
			
			// For now, only support identifiers as objects
			if (!std::holds_alternative<IdentifierNode>(object_expr)) {
				return EvalResult::error("Member access currently only supported on simple identifiers");
			}
			
			const IdentifierNode& object_id = std::get<IdentifierNode>(object_expr);
			std::string_view object_name = object_id.name();
			
			// Look up the object in bindings
			auto it = bindings.find(object_name);
			if (it == bindings.end()) {
				return EvalResult::error("Undefined object in member access: " + std::string(object_name));
			}
			
			const EvalResult& object_result = it->second;
			
			// Check if it's an ObjectValue
			if (!std::holds_alternative<ObjectValue>(object_result.value)) {
				return EvalResult::error("Member access on non-object type");
			}
			
			const ObjectValue& obj = std::get<ObjectValue>(object_result.value);
			std::string member_name_str(member_access.member_name());
			
			// Look up the member in the object
			auto member_it = obj.members.find(member_name_str);
			if (member_it == obj.members.end()) {
				return EvalResult::error("Object does not have member: " + member_name_str);
			}
			
			// Convert the member value to EvalResult
			const auto& member_value = member_it->second;
			if (std::holds_alternative<bool>(member_value)) {
				return EvalResult::from_bool(std::get<bool>(member_value));
			} else if (std::holds_alternative<long long>(member_value)) {
				return EvalResult::from_int(std::get<long long>(member_value));
			} else if (std::holds_alternative<unsigned long long>(member_value)) {
				return EvalResult::from_uint(std::get<unsigned long long>(member_value));
			} else if (std::holds_alternative<double>(member_value)) {
				return EvalResult::from_double(std::get<double>(member_value));
			}
			
			return EvalResult::error("Unsupported member value type");
		}
		
		// For literals and other expressions without parameters, evaluate normally
		return evaluate(expr_node, context);
	}

	// Helper functions for overflow-safe arithmetic using compiler builtins
private:
	// Perform addition with overflow checking, return result or nullopt on overflow
	static std::optional<long long> safe_add(long long a, long long b) {
		long long result;
		bool overflow = __builtin_add_overflow(a, b, &result);
		return overflow ? std::nullopt : std::optional<long long>(result);
	}

	// Perform subtraction with overflow checking, return result or nullopt on overflow
	static std::optional<long long> safe_sub(long long a, long long b) {
		long long result;
		bool overflow = __builtin_sub_overflow(a, b, &result);
		return overflow ? std::nullopt : std::optional<long long>(result);
	}

	// Perform multiplication with overflow checking, return result or nullopt on overflow
	static std::optional<long long> safe_mul(long long a, long long b) {
		long long result;
		bool overflow = __builtin_mul_overflow(a, b, &result);
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
};

} // namespace ConstExpr
