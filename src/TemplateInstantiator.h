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
#include "Log.h"
#include <vector>
#include <optional>
#include <unordered_map>

// Forward declarations
class Parser;

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
		[[maybe_unused]] Parser& parser)
		: params_(params)
		, args_(args)
		, param_map_(buildTemplateParamMap(params, args))
	{
		FLASH_LOG(Templates, Debug, "TemplateInstantiator created with ", params.size(), 
		          " params and ", args.size(), " args");
		
		// Log the parameter mapping for debugging
		for (const auto& [name, arg] : param_map_) {
			FLASH_LOG(Templates, Debug, "  Param '", name, "' -> type=", 
			          static_cast<int>(arg.base_type), ", type_index=", arg.type_index);
		}
	}
	
	/**
	 * Instantiate a function template
	 * 
	 * @param template_decl The template function declaration to instantiate
	 * @return The instantiated function declaration, or nullopt on failure
	 */
	std::optional<ASTNode> instantiate_function([[maybe_unused]] const ASTNode& template_decl) {
		FLASH_LOG(Templates, Debug, "TemplateInstantiator::instantiate_function called");
		
		// TODO: Extract function template instantiation logic from Parser::try_instantiate_template
		// This will be done in a follow-up commit to minimize risk
		
		// For now, return nullopt to indicate not yet implemented
		// The existing Parser methods will continue to be used
		return std::nullopt;
	}
	
	/**
	 * Instantiate a class template
	 * 
	 * @param template_decl The template class declaration to instantiate
	 * @return The instantiated class declaration, or nullopt on failure
	 */
	std::optional<ASTNode> instantiate_class([[maybe_unused]] const ASTNode& template_decl) {
		FLASH_LOG(Templates, Debug, "TemplateInstantiator::instantiate_class called");
		
		// TODO: Extract class template instantiation logic from Parser::try_instantiate_class_template
		// This will be done in a follow-up commit to minimize risk
		
		// For now, return nullopt to indicate not yet implemented
		// The existing Parser methods will continue to be used
		return std::nullopt;
	}
	
	/**
	 * Instantiate a variable template
	 * 
	 * @param template_decl The template variable declaration to instantiate
	 * @return The instantiated variable declaration, or nullopt on failure
	 */
	std::optional<ASTNode> instantiate_variable([[maybe_unused]] const ASTNode& template_decl) {
		FLASH_LOG(Templates, Debug, "TemplateInstantiator::instantiate_variable called");
		
		// TODO: Extract variable template instantiation logic from Parser::try_instantiate_variable_template
		// This will be done in a follow-up commit to minimize risk
		
		// For now, return nullopt to indicate not yet implemented
		// The existing Parser methods will continue to be used
		return std::nullopt;
	}
	
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
	ASTNode substitute_in_node(const ASTNode& node) {
		// TODO: Extract core substitution logic from Parser::substituteTemplateParameters
		// This will be done in a follow-up commit to minimize risk
		
		// For now, return a copy of the original node
		return node;
	}
	
	/**
	 * Substitute template parameters in a type specifier
	 * 
	 * @param type_spec The type specifier to substitute in
	 * @return The substituted type specifier
	 */
	TypeSpecifierNode substitute_in_type(const TypeSpecifierNode& type_spec) {
		// Check if this type is a template parameter
		if (type_spec.type() == Type::UserDefined || type_spec.type_index() == 0) {
			std::string_view type_name = type_spec.token().value();
			
			auto it = param_map_.find(type_name);
			if (it != param_map_.end()) {
				// Found a matching template parameter - substitute it
				const TemplateTypeArg& arg = it->second;
				
				FLASH_LOG(Templates, Debug, "substitute_in_type: substituting '", type_name,
				          "' with type=", static_cast<int>(arg.base_type), 
				          ", type_index=", arg.type_index);
				
				// Create a new TypeSpecifierNode with the substituted type
				TypeSpecifierNode result(
					arg.base_type,
					arg.type_index,
					get_type_size_bits(arg.base_type),
					type_spec.token(),
					arg.cv_qualifier
				);
				
				// Copy pointer levels
				for (const auto& ptr_level : type_spec.pointer_levels()) {
					result.add_pointer_level(ptr_level.cv_qualifier);
				}
				
				// Set reference qualifier from the template argument
				if (arg.is_rvalue_reference) {
					result.set_reference_qualifier(ReferenceQualifier::RValueReference);
				} else if (arg.is_reference) {
					result.set_reference_qualifier(ReferenceQualifier::LValueReference);
				}
				
				return result;
			}
		}
		
		// Not a template parameter, return a copy of the original
		return type_spec;
	}
	
	/**
	 * Build the instantiated name (e.g., "Vector_int" for Vector<int>)
	 * 
	 * @param base_name The base template name
	 * @return The instantiated name
	 */
	std::string_view build_instantiated_name(std::string_view base_name) {
		StringBuilder builder;
		builder.append(base_name);
		
		for (size_t i = 0; i < args_.size(); ++i) {
			builder.append(i == 0 ? "_" : "_");
			builder.append(args_[i].toString());
		}
		
		return builder.commit();
	}

private:
	// Template parameters (TemplateParameterNode AST nodes)
	[[maybe_unused]] const std::vector<ASTNode>& params_;
	
	// Template arguments (concrete types/values)
	const std::vector<TemplateTypeArg>& args_;
	
	// Parameter name to argument mapping for fast lookup
	std::unordered_map<std::string_view, TemplateTypeArg> param_map_;
};
