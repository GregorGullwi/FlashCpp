#pragma once

#include "AstNodeTypes.h"
#include "TemplateRegistry.h"
#include "Log.h"
#include <vector>
#include <optional>
#include <string_view>

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

/// TemplateInstantiationHelper - Shared utilities for template function instantiation
///
/// This class provides common functionality used by both ExpressionSubstitutor and
/// ConstExpr::Evaluator for deducing template arguments and instantiating template functions.
///
/// This consolidation eliminates code duplication and ensures consistent behavior
/// when working with template functions during:
/// - Template parameter substitution (ExpressionSubstitutor)
/// - Constant expression evaluation (ConstExpr::Evaluator)
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
	
	if (template_args.empty()) {
		FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper::tryInstantiateTemplateFunction: No template arguments to instantiate with");
		return std::nullopt;
	}
	
	FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper::tryInstantiateTemplateFunction: attempting to instantiate '", 
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
	
	FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper::tryInstantiateTemplateFunction: Failed to instantiate '", qualified_name, "'");
	return std::nullopt;
}
