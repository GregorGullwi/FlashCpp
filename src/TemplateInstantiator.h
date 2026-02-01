#pragma once

/**
 * TemplateInstantiator.h - Template Instantiation Logic
 * =====================================================
 * 
 * This file contains the TemplateInstantiator class which encapsulates
 * template instantiation logic for functions, classes, and variables.
 * 
 * ## Design Goals
 * 
 * 1. **Centralized Logic**: All template instantiation code in one place
 * 2. **Reusable Substitution**: Shared substitute_in_node() for all template kinds
 * 3. **Type Safety**: Uses TypeIndex-based lookups where possible
 * 4. **Testability**: Isolated from parser state for easier testing
 * 
 * ## Usage
 * 
 * ```cpp
 * // Create instantiator with template parameters and arguments
 * TemplateInstantiator instantiator(template_params, template_args);
 * 
 * // Instantiate a class template
 * auto result = instantiator.instantiate_class(template_decl);
 * 
 * // Instantiate a function template
 * auto result = instantiator.instantiate_function(template_decl);
 * ```
 */

#include "AstNodeTypes.h"
#include "TemplateRegistry.h"
#include "StringTable.h"
#include <vector>
#include <optional>
#include <unordered_map>

// Forward declarations
class Parser;

/**
 * TemplateInstantiator - Encapsulates template instantiation logic
 * 
 * This class handles the instantiation of template functions, classes,
 * and variables by substituting template parameters with concrete arguments.
 */
class TemplateInstantiator {
public:
	/**
	 * Construct a TemplateInstantiator with the given parameters and arguments
	 * 
	 * @param params Vector of TemplateParameterNode AST nodes
	 * @param args Vector of TemplateTypeArg representing the concrete arguments
	 * @param parser Reference to the parser for node creation and lookups
	 */
	TemplateInstantiator(
		const std::vector<ASTNode>& params,
		const std::vector<TemplateTypeArg>& args,
		Parser& parser);
	
	/**
	 * Instantiate a function template
	 * 
	 * @param template_decl The template function declaration to instantiate
	 * @return The instantiated function declaration, or nullopt on failure
	 */
	std::optional<ASTNode> instantiate_function(const ASTNode& template_decl);
	
	/**
	 * Instantiate a class template
	 * 
	 * @param template_decl The template class declaration to instantiate
	 * @return The instantiated class declaration, or nullopt on failure
	 */
	std::optional<ASTNode> instantiate_class(const ASTNode& template_decl);
	
	/**
	 * Instantiate a variable template
	 * 
	 * @param template_decl The template variable declaration to instantiate
	 * @return The instantiated variable declaration, or nullopt on failure
	 */
	std::optional<ASTNode> instantiate_variable(const ASTNode& template_decl);
	
	/**
	 * Get the parameter-to-argument mapping
	 * 
	 * @return Map from parameter name to TemplateTypeArg
	 */
	const std::unordered_map<std::string_view, TemplateTypeArg>& getParamMap() const {
		return param_map_;
	}
	
	/**
	 * Check if a name is a template parameter
	 * 
	 * @param name The name to check
	 * @return True if name is a template parameter
	 */
	bool isTemplateParameter(std::string_view name) const {
		return param_map_.find(name) != param_map_.end();
	}
	
	/**
	 * Get the argument for a template parameter
	 * 
	 * @param name The parameter name
	 * @return The TemplateTypeArg if found, nullopt otherwise
	 */
	std::optional<TemplateTypeArg> getArgumentForParameter(std::string_view name) const {
		auto it = param_map_.find(name);
		if (it != param_map_.end()) {
			return it->second;
		}
		return std::nullopt;
	}

private:
	/**
	 * Substitute template parameters in an AST node
	 * 
	 * This is the core substitution logic used by all instantiate_* methods.
	 * It recursively traverses the AST and replaces template parameter
	 * references with their concrete argument values.
	 * 
	 * @param node The AST node to substitute in
	 * @return The substituted AST node
	 */
	ASTNode substitute_in_node(const ASTNode& node);
	
	/**
	 * Substitute template parameters in a type specifier
	 * 
	 * @param type_spec The type specifier to substitute in
	 * @return The substituted type specifier
	 */
	TypeSpecifierNode substitute_in_type(const TypeSpecifierNode& type_spec);
	
	/**
	 * Build the instantiated name (e.g., "Vector_int" for Vector<int>)
	 * 
	 * @param base_name The base template name
	 * @return The instantiated name
	 */
	std::string_view build_instantiated_name(std::string_view base_name);

	// Template parameters (TemplateParameterNode AST nodes)
	const std::vector<ASTNode>& params_;
	
	// Template arguments (concrete types/values)
	const std::vector<TemplateTypeArg>& args_;
	
	// Reference to parser for node creation
	Parser& parser_;
	
	// Parameter name to argument mapping for fast lookup
	std::unordered_map<std::string_view, TemplateTypeArg> param_map_;
};

// ============================================================================
// Helper functions for template instantiation
// ============================================================================

/**
 * Build a map from parameter names to template arguments
 * 
 * @param params Vector of TemplateParameterNode AST nodes
 * @param args Vector of TemplateTypeArg arguments
 * @return Map from parameter name to TemplateTypeArg
 */
inline std::unordered_map<std::string_view, TemplateTypeArg> buildTemplateParamMap(
	const std::vector<ASTNode>& params,
	const std::vector<TemplateTypeArg>& args)
{
	std::unordered_map<std::string_view, TemplateTypeArg> param_map;
	
	for (size_t i = 0; i < params.size() && i < args.size(); ++i) {
		if (params[i].is<TemplateParameterNode>()) {
			const TemplateParameterNode& param = params[i].as<TemplateParameterNode>();
			param_map[param.name()] = args[i];
		}
	}
	
	return param_map;
}
