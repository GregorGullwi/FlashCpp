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
 * // Check if a name is a template parameter
 * if (instantiator.isTemplateParameter("T")) {
 *     auto arg = instantiator.getArgumentForParameter("T");
 *     // Use arg->base_type, arg->type_index, etc.
 * }
 * 
 * // Substitute types
 * TypeSpecifierNode result = instantiator.substitute_in_type(original_type);
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
 * Build a map from parameter names to TemplateArgument (used with substituteTemplateParameters)
 * 
 * @param params Vector of TemplateParameterNode AST nodes
 * @param args Vector of TemplateTypeArg arguments
 * @return Vector of TemplateArgument
 */
inline std::vector<TemplateArgument> buildTemplateArgumentsFromTypeArgs(
	const std::vector<TemplateTypeArg>& args)
{
	std::vector<TemplateArgument> result;
	result.reserve(args.size());
	
	for (const auto& arg : args) {
		TemplateArgument ta;
		if (arg.is_value_arg) {
			ta.kind = TemplateArgument::Kind::Value;
			ta.int_value = arg.int_value;
			ta.value_type = arg.base_type;
		} else {
			ta.kind = TemplateArgument::Kind::Type;
			ta.type_value = arg.base_type;
			// Create a TypeSpecifierNode for the argument
			TypeSpecifierNode& type_spec = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(
				arg.base_type,
				arg.type_index,
				get_type_size_bits(arg.base_type),
				Token{},
				arg.cv_qualifier
			);
			if (arg.is_rvalue_reference) {
				type_spec.set_reference_qualifier(ReferenceQualifier::RValueReference);
			} else if (arg.is_reference) {
				type_spec.set_reference_qualifier(ReferenceQualifier::LValueReference);
			}
			for (uint8_t i = 0; i < arg.pointer_depth; ++i) {
				type_spec.add_pointer_level(CVQualifier::None);
			}
			ta.type_specifier = type_spec;
		}
		result.push_back(ta);
	}
	
	return result;
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
	 */
	TemplateInstantiator(
		const std::vector<ASTNode>& params,
		const std::vector<TemplateTypeArg>& args)
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
	 * Get the template parameters
	 */
	const std::vector<ASTNode>& getParams() const { return params_; }
	
	/**
	 * Get the template arguments
	 */
	const std::vector<TemplateTypeArg>& getArgs() const { return args_; }
	
	/**
	 * Get TemplateArguments suitable for use with Parser::substituteTemplateParameters
	 */
	std::vector<TemplateArgument> getTemplateArguments() const {
		return buildTemplateArgumentsFromTypeArgs(args_);
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
	 * Get the argument index for a template parameter
	 * 
	 * @param name The parameter name
	 * @return The index if found, nullopt otherwise
	 */
	std::optional<size_t> getArgumentIndex(std::string_view name) const {
		for (size_t i = 0; i < params_.size(); ++i) {
			if (params_[i].is<TemplateParameterNode>()) {
				const TemplateParameterNode& param = params_[i].as<TemplateParameterNode>();
				if (param.name() == name) {
					return i;
				}
			}
		}
		return std::nullopt;
	}

	/**
	 * Substitute template parameters in a type specifier
	 * 
	 * This is the primary method for type substitution. It handles:
	 * - Direct parameter substitution (T -> int)
	 * - Preserving pointer levels from both original and argument
	 * - Preserving reference qualifiers
	 * - Preserving CV qualifiers
	 * 
	 * @param type_spec The type specifier to substitute in
	 * @return The substituted type specifier
	 */
	TypeSpecifierNode substitute_in_type(const TypeSpecifierNode& type_spec) const {
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
				
				// Copy pointer levels from original type spec (e.g., T* keeps the *)
				for (const auto& ptr_level : type_spec.pointer_levels()) {
					result.add_pointer_level(ptr_level.cv_qualifier);
				}
				
				// Also add pointer levels from the argument (e.g., T = int* adds *)
				for (uint8_t i = 0; i < arg.pointer_depth; ++i) {
					result.add_pointer_level(CVQualifier::None);
				}
				
				// Set reference qualifier from the template argument
				if (arg.is_rvalue_reference) {
					result.set_reference_qualifier(ReferenceQualifier::RValueReference);
				} else if (arg.is_reference) {
					result.set_reference_qualifier(ReferenceQualifier::LValueReference);
				} else if (type_spec.reference_qualifier() != ReferenceQualifier::None) {
					// Preserve original reference if argument doesn't specify one
					result.set_reference_qualifier(type_spec.reference_qualifier());
				}
				
				return result;
			}
		}
		
		// Not a template parameter, return a copy of the original
		return type_spec;
	}
	
	/**
	 * Get the substituted type info for a parameter
	 * 
	 * This is a simplified interface that returns just Type and TypeIndex,
	 * matching the signature of Parser::substitute_template_parameter.
	 * 
	 * @param original_type The original type specifier
	 * @return Pair of (Type, TypeIndex) for the substituted type
	 */
	std::pair<Type, TypeIndex> substituteType(const TypeSpecifierNode& original_type) const {
		TypeSpecifierNode result = substitute_in_type(original_type);
		return {result.type(), result.type_index()};
	}
	
	/**
	 * Build the instantiated name (e.g., "Vector_int" for Vector<int>)
	 * 
	 * @param base_name The base template name
	 * @return The instantiated name
	 */
	std::string_view build_instantiated_name(std::string_view base_name) const {
		StringBuilder builder;
		builder.append(base_name);
		
		for (size_t i = 0; i < args_.size(); ++i) {
			builder.append("_");
			builder.append(args_[i].toString());
		}
		
		return builder.commit();
	}
	
	/**
	 * Build a TypeIndex-based key for this instantiation
	 * 
	 * @param template_name_handle StringHandle of the template name
	 * @return TemplateInstantiationKeyV2 for cache lookups
	 */
	FlashCpp::TemplateInstantiationKeyV2 buildInstantiationKey(StringHandle template_name_handle) const {
		return FlashCpp::makeInstantiationKeyV2(template_name_handle, args_);
	}

private:
	// Template parameters (TemplateParameterNode AST nodes)
	const std::vector<ASTNode>& params_;
	
	// Template arguments (concrete types/values)
	const std::vector<TemplateTypeArg>& args_;
	
	// Parameter name to argument mapping for fast lookup
	std::unordered_map<std::string_view, TemplateTypeArg> param_map_;
};
