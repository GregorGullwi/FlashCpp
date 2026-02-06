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

	// Check if evaluation was successful
	bool success() const {
		return error_type == EvalErrorType::None;
	}

	// Convenience constructors
	static EvalResult from_bool(bool val) {
		return EvalResult{val, "", EvalErrorType::None, false, {}};
	}

	static EvalResult from_int(long long val) {
		return EvalResult{val, "", EvalErrorType::None, false, {}};
	}

	static EvalResult from_uint(unsigned long long val) {
		return EvalResult{val, "", EvalErrorType::None, false, {}};
	}

	static EvalResult from_double(double val) {
		return EvalResult{val, "", EvalErrorType::None, false, {}};
	}

	static EvalResult error(const std::string& msg, EvalErrorType type = EvalErrorType::Other) {
		return EvalResult{false, msg, type, false, {}};
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
	
	// Parser pointer for template instantiation (optional)
	Parser* parser = nullptr;

	// Constructor requires symbol table to prevent missing it
	explicit EvaluationContext(const SymbolTable& symbol_table)
		: symbols(&symbol_table) {}
};

// Main constant expression evaluator class

// The Evaluator class is split across multiple files for maintainability.
#include "ConstExprEvaluator_Core.cpp"      // Class declaration, evaluate(), numeric/binary/unary operators, sizeof/alignof, casts, identifier, builtin functions, function calls
#include "ConstExprEvaluator_Members.cpp"   // Expression bindings, qualified identifiers, member access, array subscripts, type traits

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
