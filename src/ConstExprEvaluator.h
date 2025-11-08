#pragma once

#include "AstNodeTypes.h"
#include <optional>
#include <string>
#include <variant>

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

// Context for evaluation - provides access to compile-time information
struct EvaluationContext {
	// Symbol table for looking up constexpr variables/functions (future use)
	const SymbolTable* symbols = nullptr;

	// Type information for sizeof, alignof, etc. (future use)
	const TypeInfo* type_info = nullptr;

	// Maximum recursion depth for constexpr functions (future use)
	size_t max_recursion_depth = 512;

	// Track current recursion depth (future use)
	size_t current_depth = 0;
};

// Main constant expression evaluator class
class Evaluator {
public:
	// Main evaluation entry point
	// Evaluates a constant expression and returns the result
	static EvalResult evaluate(const ASTNode& expr_node, const EvaluationContext& context) {
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
	                                            std::string_view op, const EvaluationContext& context) {
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
	                                           const EvaluationContext& context) {
		// Recursively evaluate operand
		auto operand_result = evaluate(operand_node, context);

		if (!operand_result.success) {
			return operand_result;
		}

		return apply_unary_op(operand_result, op);
	}

	static EvalResult evaluate_sizeof(const SizeofExprNode& sizeof_expr, const EvaluationContext& context) {
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
