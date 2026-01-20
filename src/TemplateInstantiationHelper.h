#pragma once

#include "AstNodeTypes.h"
#include "TemplateRegistry.h"
#include "Log.h"
#include <vector>
#include <optional>
#include <string_view>

// Forward declarations
class Parser;

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
	/// @param arguments The function call arguments to analyze
	/// @return A vector of deduced template type arguments
	static std::vector<TemplateTypeArg> deduceTemplateArgsFromCall(
		const ChunkedVector<ASTNode>& arguments);

	/// Try to instantiate a template function with deduced or explicit arguments
	///
	/// This method attempts to instantiate a template function, trying various
	/// name variations (qualified name, simple name, with namespace prefixes).
	///
	/// @param parser Reference to the parser for triggering template instantiation
	/// @param qualified_name The qualified function name (e.g., "std::__is_complete_or_unbounded")
	/// @param simple_name The simple function name (e.g., "__is_complete_or_unbounded")
	/// @param template_args The template arguments to instantiate with
	/// @return The instantiated function declaration if successful, std::nullopt otherwise
	static std::optional<ASTNode> tryInstantiateTemplateFunction(
		Parser& parser,
		std::string_view qualified_name,
		std::string_view simple_name,
		const std::vector<TemplateTypeArg>& template_args);

	/// Get common namespace prefixes to try when looking up template functions
	///
	/// @param func_name The function name to prefix
	/// @return A vector of candidate names with common namespace prefixes
	static std::vector<std::string> getNamespaceCandidates(std::string_view func_name);
};

// Implementation (inline for header-only usage)

inline std::vector<TemplateTypeArg> TemplateInstantiationHelper::deduceTemplateArgsFromCall(
	const ChunkedVector<ASTNode>& arguments) {
	
	std::vector<TemplateTypeArg> deduced_args;
	
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
					FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper: Deduced template argument from constructor call arg ", i);
				}
			}
		}
	}
	
	return deduced_args;
}

inline std::vector<std::string> TemplateInstantiationHelper::getNamespaceCandidates(std::string_view func_name) {
	std::vector<std::string> candidates;
	candidates.push_back(std::string("std::") + std::string(func_name));
	candidates.push_back(std::string("__gnu_cxx::") + std::string(func_name));
	return candidates;
}

inline std::optional<ASTNode> TemplateInstantiationHelper::tryInstantiateTemplateFunction(
	Parser& parser,
	std::string_view qualified_name,
	std::string_view simple_name,
	const std::vector<TemplateTypeArg>& template_args) {
	
	if (template_args.empty()) {
		FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper: No template arguments to instantiate with");
		return std::nullopt;
	}
	
	FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper: Attempting to instantiate template function with ", 
	          template_args.size(), " arguments");
	
	// Try qualified name first
	auto instantiated_opt = parser.try_instantiate_template_explicit(qualified_name, template_args);
	if (instantiated_opt.has_value()) {
		FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper: Instantiated with qualified name: ", qualified_name);
		return instantiated_opt;
	}
	
	// Try simple name if different from qualified name
	if (qualified_name != simple_name) {
		instantiated_opt = parser.try_instantiate_template_explicit(simple_name, template_args);
		if (instantiated_opt.has_value()) {
			FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper: Instantiated with simple name: ", simple_name);
			return instantiated_opt;
		}
	}
	
	// Try with common namespace prefixes
	auto name_candidates = getNamespaceCandidates(simple_name);
	for (const auto& candidate_name : name_candidates) {
		instantiated_opt = parser.try_instantiate_template_explicit(candidate_name, template_args);
		if (instantiated_opt.has_value()) {
			FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper: Instantiated with namespace prefix: ", candidate_name);
			return instantiated_opt;
		}
	}
	
	FLASH_LOG(Templates, Debug, "TemplateInstantiationHelper: Failed to instantiate template function: ", qualified_name);
	return std::nullopt;
}
