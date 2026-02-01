/**
 * TemplateInstantiator.cpp - Template Instantiation Implementation
 * ================================================================
 * 
 * Implementation of the TemplateInstantiator class for template
 * function, class, and variable instantiation.
 */

#include "TemplateInstantiator.h"
#include "Parser.h"
#include "Log.h"

// ============================================================================
// Constructor
// ============================================================================

TemplateInstantiator::TemplateInstantiator(
	const std::vector<ASTNode>& params,
	const std::vector<TemplateTypeArg>& args,
	Parser& parser)
	: params_(params)
	, args_(args)
	, parser_(parser)
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

// ============================================================================
// Instantiation Methods
// ============================================================================

std::optional<ASTNode> TemplateInstantiator::instantiate_function([[maybe_unused]] const ASTNode& template_decl) {
	FLASH_LOG(Templates, Debug, "TemplateInstantiator::instantiate_function called");
	
	// TODO: Extract function template instantiation logic from Parser::try_instantiate_template
	// This will be done in a follow-up commit to minimize risk
	
	// For now, return nullopt to indicate not yet implemented
	// The existing Parser methods will continue to be used
	return std::nullopt;
}

std::optional<ASTNode> TemplateInstantiator::instantiate_class([[maybe_unused]] const ASTNode& template_decl) {
	FLASH_LOG(Templates, Debug, "TemplateInstantiator::instantiate_class called");
	
	// TODO: Extract class template instantiation logic from Parser::try_instantiate_class_template
	// This will be done in a follow-up commit to minimize risk
	
	// For now, return nullopt to indicate not yet implemented
	// The existing Parser methods will continue to be used
	return std::nullopt;
}

std::optional<ASTNode> TemplateInstantiator::instantiate_variable([[maybe_unused]] const ASTNode& template_decl) {
	FLASH_LOG(Templates, Debug, "TemplateInstantiator::instantiate_variable called");
	
	// TODO: Extract variable template instantiation logic from Parser::try_instantiate_variable_template
	// This will be done in a follow-up commit to minimize risk
	
	// For now, return nullopt to indicate not yet implemented
	// The existing Parser methods will continue to be used
	return std::nullopt;
}

// ============================================================================
// Substitution Methods
// ============================================================================

ASTNode TemplateInstantiator::substitute_in_node(const ASTNode& node) {
	// TODO: Extract core substitution logic from Parser::substituteTemplateParameters
	// This will be done in a follow-up commit to minimize risk
	
	// For now, return a copy of the original node
	return node;
}

TypeSpecifierNode TemplateInstantiator::substitute_in_type(const TypeSpecifierNode& type_spec) {
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
			
			// Copy reference qualifier
			if (arg.is_rvalue_reference) {
				result.set_reference(true);  // rvalue reference
			} else if (arg.is_reference) {
				result.set_reference(false);  // lvalue reference
			}
			
			return result;
		}
	}
	
	// Not a template parameter, return a copy of the original
	return type_spec;
}

std::string_view TemplateInstantiator::build_instantiated_name(std::string_view base_name) {
	StringBuilder builder;
	builder.append(base_name);
	
	for (size_t i = 0; i < args_.size(); ++i) {
		builder.append(i == 0 ? "_" : "_");
		builder.append(args_[i].toString());
	}
	
	return builder.commit();
}
