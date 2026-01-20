#pragma once

#include "AstNodeTypes.h"
#include "TemplateRegistry.h"
#include "Log.h"
#include <vector>
#include <optional>
#include <string_view>
#include <string>

// Forward declarations
class Parser;

/// @file TemplateInstantiationHelper.h
/// @brief Shared utilities for template function instantiation used by both
///        ExpressionSubstitutor and ConstExpr::Evaluator.
///
/// ## Architecture Overview
///
/// This helper class consolidates template argument deduction and instantiation
/// logic that was previously duplicated between ExpressionSubstitutor and
/// ConstExpr::Evaluator. Both classes work together in template instantiation:
///
/// ### Flow: Template Parameter Substitution (ExpressionSubstitutor)
/// ```
/// Parser.instantiate_template()
///   → ExpressionSubstitutor.substitute()     // Replace T with int
///     → TemplateInstantiationHelper          // Deduce & instantiate nested calls
///       → Parser.try_instantiate_template_explicit()
///     → Modified AST
/// ```
///
/// ### Flow: Constant Expression Evaluation (ConstExpr::Evaluator)
/// ```
/// Parser.parse_static_assert()
///   → ConstExpr::Evaluator.evaluate()        // Compute value
///     → TemplateInstantiationHelper          // Instantiate template if needed
///       → Parser.try_instantiate_template_explicit()
///     → Primitive value (int/bool/double)
/// ```
///
/// ### Key Differences Between Callers
///
/// | Aspect          | ExpressionSubstitutor        | ConstExpr::Evaluator        |
/// |-----------------|------------------------------|-----------------------------|
/// | **Purpose**     | AST transformation           | Value computation           |
/// | **Phase**       | Template instantiation       | Constexpr evaluation        |
/// | **Input**       | AST with template params     | AST with concrete types     |
/// | **Output**      | Modified AST                 | Primitive value             |
/// | **When Called** | decltype in base class, etc. | static_assert, constexpr    |
///
/// @see ExpressionSubstitutor for template parameter substitution
/// @see ConstExpr::Evaluator for constant expression evaluation

/// Error information for template instantiation failures
struct TemplateInstantiationError {
	std::string function_name;
	std::string reason;
	size_t arg_count = 0;
	
	std::string format() const {
		std::string msg = "Template instantiation failed for '" + function_name + "'";
		if (!reason.empty()) {
			msg += ": " + reason;
		}
		if (arg_count > 0) {
			msg += " (with " + std::to_string(arg_count) + " template argument(s))";
		}
		return msg;
	}
};

/// TemplateInstantiationHelper - Shared utilities for template function instantiation
///
/// This class provides common functionality used by both ExpressionSubstitutor and
/// ConstExpr::Evaluator for deducing template arguments and instantiating template functions.
///
/// This consolidation eliminates code duplication and ensures consistent behavior
/// when working with template functions during:
/// - Template parameter substitution (ExpressionSubstitutor)
/// - Constant expression evaluation (ConstExpr::Evaluator)
///
/// ## Supported Deduction Patterns
///
/// ### 1. Constructor Call Pattern (Phase 1)
/// ```cpp
/// func(__type_identity<int>{})  // Deduces T = int from type wrapper
/// ```
///
/// ### 2. Function Parameter Type Pattern (Phase 3)
/// ```cpp
/// template<typename T> void foo(T x);
/// foo(42);  // Deduces T = int from argument type
/// ```
///
/// ### 3. Template Template Parameters (Phase 3)
/// ```cpp
/// template<template<typename...> class C, typename T>
/// void bar(C<T> container);  // Deduces C and T from container type
/// ```
class TemplateInstantiationHelper {
public:
	/// Deduce template arguments from function call arguments
	///
	/// This handles the common pattern of deducing template arguments from
	/// constructor call patterns like `func(__type_identity<int>{})`.
	///
	/// The pattern works because type wrapper templates like `__type_identity`
	/// carry their template argument in their type, which we can extract.
	///
	/// @param arguments The function call arguments to analyze
	/// @return A vector of deduced template type arguments
	///
	/// @note Used by both ExpressionSubstitutor::substituteFunctionCall() and
	///       ConstExpr::Evaluator::evaluate_function_call()
	static std::vector<TemplateTypeArg> deduceTemplateArgsFromCall(
		const ChunkedVector<ASTNode>& arguments);

	/// Deduce template arguments from function parameter types (Phase 3)
	///
	/// This handles deducing template arguments by matching actual argument types
	/// against function parameter types. For example:
	/// ```cpp
	/// template<typename T> void foo(T x);
	/// foo(42);  // T deduced as int
	/// ```
	///
	/// @param param_types The function parameter types from the template declaration
	/// @param arg_types The actual argument types from the call
	/// @return A vector of deduced template type arguments, empty if deduction fails
	static std::vector<TemplateTypeArg> deduceTemplateArgsFromParamTypes(
		const std::vector<TypeSpecifierNode>& param_types,
		const std::vector<TypeSpecifierNode>& arg_types);

	/// Try to instantiate a template function with deduced or explicit arguments
	///
	/// This method attempts to instantiate a template function by trying the
	/// qualified name first, then falling back to the simple name if different.
	///
	/// @param parser Reference to the parser for triggering template instantiation
	/// @param qualified_name The qualified function name (e.g., "std::__is_complete_or_unbounded")
	/// @param simple_name The simple function name (e.g., "__is_complete_or_unbounded")
	/// @param template_args The template arguments to instantiate with
	/// @return The instantiated function declaration if successful, std::nullopt otherwise
	///
	/// @note Used by both ExpressionSubstitutor::substituteFunctionCall() and
	///       ConstExpr::Evaluator::evaluate_function_call()
	static std::optional<ASTNode> tryInstantiateTemplateFunction(
		Parser& parser,
		std::string_view qualified_name,
		std::string_view simple_name,
		const std::vector<TemplateTypeArg>& template_args);

	/// Try to instantiate with detailed error reporting (Phase 3)
	///
	/// Like tryInstantiateTemplateFunction but returns detailed error information
	/// when instantiation fails.
	///
	/// @param parser Reference to the parser for triggering template instantiation
	/// @param qualified_name The qualified function name
	/// @param simple_name The simple function name
	/// @param template_args The template arguments to instantiate with
	/// @param error_out If provided, populated with error details on failure
	/// @return The instantiated function declaration if successful, std::nullopt otherwise
	static std::optional<ASTNode> tryInstantiateWithErrorInfo(
		Parser& parser,
		std::string_view qualified_name,
		std::string_view simple_name,
		const std::vector<TemplateTypeArg>& template_args,
		TemplateInstantiationError* error_out);

	/// Check if a type represents a template template parameter (Phase 3)
	///
	/// Template template parameters have the form:
	/// ```cpp
	/// template<template<typename...> class Container>
	/// ```
	///
	/// @param type_spec The type specifier to check
	/// @return true if this is a template template parameter
	static bool isTemplateTemplateParameter(const TypeSpecifierNode& type_spec);
};

// Implementation (inline for header-only usage)

inline std::vector<TemplateTypeArg> TemplateInstantiationHelper::deduceTemplateArgsFromCall(
	const ChunkedVector<ASTNode>& arguments) {
	
	std::vector<TemplateTypeArg> deduced_args;
	
	FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper::deduceTemplateArgsFromCall: analyzing ", 
	          arguments.size(), " arguments");
	
	for (size_t i = 0; i < arguments.size(); ++i) {
		const ASTNode& arg = arguments[i];
		
		// Check if argument is a ConstructorCallNode (like __type_identity<int>{})
		if (arg.is<ExpressionNode>()) {
			const ExpressionNode& expr = arg.as<ExpressionNode>();
			if (std::holds_alternative<ConstructorCallNode>(expr)) {
				const ConstructorCallNode& ctor = std::get<ConstructorCallNode>(expr);
				const ASTNode& type_node = ctor.type_node();
				if (type_node.is<TypeSpecifierNode>()) {
					const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
					// Use this type as a template argument
					deduced_args.emplace_back(type_spec);
					FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper: Deduced template argument from constructor call arg ", i,
					          " (type_index=", type_spec.type_index(), ")");
				}
			}
		}
	}
	
	FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper::deduceTemplateArgsFromCall: deduced ", 
	          deduced_args.size(), " template arguments");
	
	return deduced_args;
}

inline std::optional<ASTNode> TemplateInstantiationHelper::tryInstantiateTemplateFunction(
	Parser& parser,
	std::string_view qualified_name,
	std::string_view simple_name,
	const std::vector<TemplateTypeArg>& template_args) {
	
	// Delegate to the error-reporting version but ignore the error details
	return tryInstantiateWithErrorInfo(parser, qualified_name, simple_name, template_args, nullptr);
}

inline std::optional<ASTNode> TemplateInstantiationHelper::tryInstantiateWithErrorInfo(
	Parser& parser,
	std::string_view qualified_name,
	std::string_view simple_name,
	const std::vector<TemplateTypeArg>& template_args,
	TemplateInstantiationError* error_out) {
	
	if (template_args.empty()) {
		FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper::tryInstantiateWithErrorInfo: No template arguments to instantiate with");
		if (error_out) {
			error_out->function_name = std::string(qualified_name);
			error_out->reason = "no template arguments provided";
			error_out->arg_count = 0;
		}
		return std::nullopt;
	}
	
	FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper::tryInstantiateWithErrorInfo: attempting to instantiate '", 
	          qualified_name, "' with ", template_args.size(), " arguments");
	
	// Try qualified name first
	auto instantiated_opt = parser.try_instantiate_template_explicit(qualified_name, template_args);
	if (instantiated_opt.has_value()) {
		FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper: Instantiated with qualified name: ", qualified_name);
		return instantiated_opt;
	}
	
	// Try simple name if different from qualified name
	if (qualified_name != simple_name) {
		FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper: Trying simple name: ", simple_name);
		instantiated_opt = parser.try_instantiate_template_explicit(simple_name, template_args);
		if (instantiated_opt.has_value()) {
			FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper: Instantiated with simple name: ", simple_name);
			return instantiated_opt;
		}
	}
	
	// Provide detailed error information
	if (error_out) {
		error_out->function_name = std::string(qualified_name);
		error_out->arg_count = template_args.size();
		
		// Try to determine the reason for failure
		if (qualified_name.empty()) {
			error_out->reason = "empty function name";
		} else {
			error_out->reason = "template not found or argument mismatch";
		}
	}
	
	FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper::tryInstantiateWithErrorInfo: Failed to instantiate '", qualified_name, "'");
	return std::nullopt;
}

inline std::vector<TemplateTypeArg> TemplateInstantiationHelper::deduceTemplateArgsFromParamTypes(
	const std::vector<TypeSpecifierNode>& param_types,
	const std::vector<TypeSpecifierNode>& arg_types) {
	
	std::vector<TemplateTypeArg> deduced_args;
	
	FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper::deduceTemplateArgsFromParamTypes: ",
	          param_types.size(), " params, ", arg_types.size(), " args");
	
	// Simple type matching: if argument count matches, use argument types directly
	// This handles cases like: template<typename T> void foo(T x); foo(42);
	// where we deduce T = int from the argument type
	
	size_t min_count = std::min(param_types.size(), arg_types.size());
	for (size_t i = 0; i < min_count; ++i) {
		const TypeSpecifierNode& param = param_types[i];
		const TypeSpecifierNode& arg = arg_types[i];
		
		// If the parameter is a template parameter (dependent type), deduce from argument
		if (param.type() == Type::Template) {
			// This is a template parameter - use the argument type as the deduced type
			deduced_args.emplace_back(arg);
			FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper: Deduced type from param ", i,
			          " (arg type_index=", arg.type_index(), ")");
		}
		// Check for template template parameter patterns (e.g., Container<T>)
		else if (isTemplateTemplateParameter(param)) {
			// For template template parameters, we need special handling
			// The argument type should be an instantiation of a template
			deduced_args.emplace_back(arg);
			FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper: Deduced template template arg from param ", i);
		}
	}
	
	FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper::deduceTemplateArgsFromParamTypes: deduced ",
	          deduced_args.size(), " arguments");
	
	return deduced_args;
}

inline bool TemplateInstantiationHelper::isTemplateTemplateParameter(const TypeSpecifierNode& type_spec) {
	// A template template parameter is detected when:
	// 1. The type is marked as a template type (Type::Template)
	// 2. AND it has nested template arguments itself (like Container<T>)
	//
	// This is a simplified check - full template template parameter detection
	// would require more context from the template declaration
	
	if (type_spec.type() == Type::Template) {
		// Check if this template type has template arguments
		// (indicating it's something like Container<T> rather than just T)
		TypeIndex idx = type_spec.type_index();
		if (idx < gTypeInfo.size()) {
			const TypeInfo& info = gTypeInfo[idx];
			// Template template parameters typically have nested template args
			// indicated by the type name containing '<'
			std::string_view name = StringTable::getStringView(info.name());
			if (name.find('<') != std::string_view::npos) {
				FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper::isTemplateTemplateParameter: ",
				          "detected template template parameter: ", name);
				return true;
			}
		}
	}
	return false;
}
