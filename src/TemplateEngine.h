#pragma once

#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <limits>
#include "Parser.h"
#include "MigrationStats.h"

class TemplateEngine {
public:
	TemplateEngine() = default;

	void attach(Parser& parser);

	std::optional<ASTNode> tryInstantiateTemplate(
		std::string_view template_name,
		std::span<const TypeSpecifierNode> arg_types);

	std::optional<ASTNode> tryInstantiateTemplateExplicit(
		std::string_view template_name,
		std::span<const TemplateTypeArg> explicit_types,
		size_t call_arg_count);

	std::optional<ASTNode> tryInstantiateTemplateExplicit(
		std::string_view template_name,
		std::span<const TemplateTypeArg> explicit_types);

	std::optional<ASTNode> tryInstantiateTemplateExplicit(
		std::string_view template_name,
		std::span<const TemplateTypeArg> explicit_types,
		std::span<const TypeSpecifierNode> arg_types);

	std::optional<ASTNode> tryInstantiateClassTemplate(
		std::string_view template_name,
		std::span<const TemplateTypeArg> template_args,
		bool force_eager);

	std::optional<ASTNode> tryInstantiateVariableTemplate(
		std::string_view template_name,
		std::span<const TemplateTypeArg> template_args,
		const OuterTemplateBinding* explicit_outer_binding);

	std::optional<ASTNode> tryInstantiateMemberFunctionTemplate(
		std::string_view struct_name,
		std::string_view member_name,
		std::span<const TypeSpecifierNode> arg_types);

	std::optional<ASTNode> tryInstantiateMemberFunctionTemplateExplicit(
		std::string_view struct_name,
		std::string_view member_name,
		std::span<const TemplateTypeArg> template_type_args);

	std::optional<ASTNode> tryInstantiateMemberFunctionTemplateCall(
		std::string_view struct_name,
		std::string_view member_name,
		const std::optional<TemplateArgumentVector>& explicit_template_args,
		std::span<const TypeSpecifierNode> call_arg_types,
		bool has_call_args,
		bool has_dependent_call_args);

	Parser::AliasTemplateMaterializationResult materializeTemplateInstantiationForLookup(
		std::string_view template_name,
		std::span<const TemplateTypeArg> template_args);

	Parser::AliasTemplateMaterializationResult materializeAliasTemplateInstantiation(
		std::string_view alias_template_name,
		std::span<const TemplateTypeArg> template_args);

	Parser::AliasTemplateMaterializationResult materializeCanonicalOwnerTypeForLookup(
		const TypeInfo& owner_type_info,
		std::span<const TemplateTypeArg> owner_template_args);

	Parser::AliasTemplateMaterializationResult materializeCanonicalOwnerTypeForLookup(
		const TemplateTypeArg& owner_type_arg);

	Parser::ResolvedQualifiedOwner resolveQualifiedOwnerForLookup(
		std::string_view owner_name);

	std::optional<TemplateArgumentVector> materializeConcreteCallTemplateArguments(
		std::span<const ASTNode> template_argument_nodes);

	std::optional<ASTNode> resolveDefinitionBoundOrdinaryCall(
		const FunctionCallDefinitionLookupRecord& record,
		std::span<const TypeSpecifierNode> arg_types);

	std::optional<ASTNode> resolveDefinitionBoundQualifiedTemplateCall(
		const FunctionCallDefinitionLookupRecord& record,
		std::string_view qualified_name,
		std::span<const ASTNode> template_argument_nodes,
		const ChunkedVector<ASTNode>& arguments,
		std::span<const TypeSpecifierNode> arg_types);

	std::optional<ASTNode> resolveDeferredQualifiedTemplateCall(
		std::string_view qualified_name,
		std::span<const ASTNode> template_argument_nodes,
		const ChunkedVector<ASTNode>& arguments,
		std::span<const TypeSpecifierNode> arg_types);

	std::optional<ASTNode> tryInstantiateTemplateFromCallArguments(
		std::string_view qualified_name,
		std::string_view simple_name,
		const ChunkedVector<ASTNode>& arguments);

	const FunctionDeclarationNode* tryInstantiateOperatorTemplateForBinary(
		std::string_view op_symbol,
		const TypeSpecifierNode& left_type_spec,
		const TypeSpecifierNode& right_type_spec);

	std::optional<ASTNode> instantiateLazyMemberForCanonicalOwner(
		std::string_view& owner_name,
		std::string_view member_name,
		std::span<const TemplateTypeArg> owner_template_args);

	bool instantiateLazyClassToPhase(
		StringHandle instantiated_name,
		ClassInstantiationPhase target_phase);

	std::optional<TypeIndex> instantiateLazyNestedType(
		StringHandle parent_class_name,
		StringHandle nested_type_name);

	std::optional<ASTNode> instantiateLazyMemberIfNeeded(const LazyMemberKey& member_key);

	bool instantiateLazyStaticMember(
		StringHandle instantiated_class_name,
		StringHandle member_name);

	std::optional<ASTNode> instantiateLazyMemberFunction(
		const LazyMemberFunctionInfo& lazy_info,
		bool materialize_body);

	const ConstructorDeclarationNode* materializeMatchingConstructorTemplate(
		StringHandle instantiated_struct_name,
		const StructTypeInfo& struct_info,
		std::span<const TypeSpecifierNode> arg_types,
		const ConstructorDeclarationNode* preferred_ctor,
		bool& is_ambiguous);

	std::optional<ASTNode> parseTemplateBody(
		Parser::SaveHandle body_pos,
		std::span<const std::string_view> template_param_names,
		std::span<const TypeCategory> concrete_types,
		StringHandle struct_name,
		TypeIndex struct_type_index);

	ASTNode substituteTemplateParameters(
		const ASTNode& node,
		std::span<const TemplateParameterNode> template_params,
		std::span<const TemplateTypeArg> template_args);

	ASTNode substituteTemplateParameters(
		const ASTNode& node,
		std::span<const TemplateParameterNode> template_params,
		std::span<const TemplateTypeArg> template_args,
		TypeIndex current_owner_type_index,
		bool has_implicit_this);

	ASTNode substituteTemplateParameters(
		const ASTNode& node,
		const TemplateInstantiationContext& context);

	std::optional<ASTNode> instantiateClassTemplateForSignatureReplay(
		std::string_view template_name,
		std::span<const TemplateTypeArg> template_args,
		bool force_eager);

private:
	Parser& parser();

	Parser* parser_ = nullptr;
};

inline Parser& TemplateEngine::parser() {
	if (parser_ == nullptr) {
		throw InternalError("TemplateEngine used before attach()");
	}
	return *parser_;
}

inline void TemplateEngine::attach(Parser& parser) {
	parser_ = &parser;
}

inline std::optional<ASTNode> TemplateEngine::tryInstantiateTemplate(
	std::string_view template_name,
	std::span<const TypeSpecifierNode> arg_types) {
	recordTemplateEngineOldEngineRoute();
	return parser().try_instantiate_template(template_name, arg_types);
}

inline std::optional<ASTNode> TemplateEngine::tryInstantiateTemplateExplicit(
	std::string_view template_name,
	std::span<const TemplateTypeArg> explicit_types,
	size_t call_arg_count) {
	recordTemplateEngineOldEngineRoute();
	return parser().try_instantiate_template_explicit(template_name, explicit_types, call_arg_count);
}

inline std::optional<ASTNode> TemplateEngine::tryInstantiateTemplateExplicit(
	std::string_view template_name,
	std::span<const TemplateTypeArg> explicit_types) {
	recordTemplateEngineOldEngineRoute();
	return parser().try_instantiate_template_explicit(
		template_name,
		explicit_types,
		std::numeric_limits<size_t>::max());
}

inline std::optional<ASTNode> TemplateEngine::tryInstantiateTemplateExplicit(
	std::string_view template_name,
	std::span<const TemplateTypeArg> explicit_types,
	std::span<const TypeSpecifierNode> arg_types) {
	recordTemplateEngineOldEngineRoute();
	return parser().try_instantiate_template_explicit(template_name, explicit_types, arg_types);
}

inline std::optional<ASTNode> TemplateEngine::tryInstantiateClassTemplate(
	std::string_view template_name,
	std::span<const TemplateTypeArg> template_args,
	bool force_eager) {
	recordTemplateEngineOldEngineRoute();
	return parser().try_instantiate_class_template(template_name, template_args, force_eager);
}

inline std::optional<ASTNode> TemplateEngine::tryInstantiateVariableTemplate(
	std::string_view template_name,
	std::span<const TemplateTypeArg> template_args,
	const OuterTemplateBinding* explicit_outer_binding) {
	recordTemplateEngineOldEngineRoute();
	return parser().try_instantiate_variable_template(template_name, template_args, explicit_outer_binding);
}

inline std::optional<ASTNode> TemplateEngine::tryInstantiateMemberFunctionTemplate(
	std::string_view struct_name,
	std::string_view member_name,
	std::span<const TypeSpecifierNode> arg_types) {
	recordTemplateEngineOldEngineRoute();
	return parser().try_instantiate_member_function_template(struct_name, member_name, arg_types);
}

inline std::optional<ASTNode> TemplateEngine::tryInstantiateMemberFunctionTemplateExplicit(
	std::string_view struct_name,
	std::string_view member_name,
	std::span<const TemplateTypeArg> template_type_args) {
	recordTemplateEngineOldEngineRoute();
	return parser().try_instantiate_member_function_template_explicit(
		struct_name,
		member_name,
		template_type_args);
}

inline std::optional<ASTNode> TemplateEngine::tryInstantiateMemberFunctionTemplateCall(
	std::string_view struct_name,
	std::string_view member_name,
	const std::optional<TemplateArgumentVector>& explicit_template_args,
	std::span<const TypeSpecifierNode> call_arg_types,
	bool has_call_args,
	bool has_dependent_call_args) {
	recordTemplateEngineOldEngineRoute();
	return parser().tryInstantiateMemberFunctionTemplateCall(
		struct_name,
		member_name,
		explicit_template_args,
		call_arg_types,
		has_call_args,
		has_dependent_call_args);
}

inline Parser::AliasTemplateMaterializationResult TemplateEngine::materializeTemplateInstantiationForLookup(
	std::string_view template_name,
	std::span<const TemplateTypeArg> template_args) {
	recordTemplateEngineOldEngineRoute();
	return parser().materializeTemplateInstantiationForLookup(template_name, template_args);
}

inline Parser::AliasTemplateMaterializationResult TemplateEngine::materializeAliasTemplateInstantiation(
	std::string_view alias_template_name,
	std::span<const TemplateTypeArg> template_args) {
	recordTemplateEngineOldEngineRoute();
	return parser().materializeAliasTemplateInstantiation(alias_template_name, template_args);
}

inline Parser::AliasTemplateMaterializationResult TemplateEngine::materializeCanonicalOwnerTypeForLookup(
	const TypeInfo& owner_type_info,
	std::span<const TemplateTypeArg> owner_template_args) {
	recordTemplateEngineOldEngineRoute();
	return parser().materializeCanonicalOwnerTypeForLookup(owner_type_info, owner_template_args);
}

inline Parser::AliasTemplateMaterializationResult TemplateEngine::materializeCanonicalOwnerTypeForLookup(
	const TemplateTypeArg& owner_type_arg) {
	recordTemplateEngineOldEngineRoute();
	return parser().materializeCanonicalOwnerTypeForLookup(owner_type_arg);
}

inline Parser::ResolvedQualifiedOwner TemplateEngine::resolveQualifiedOwnerForLookup(
	std::string_view owner_name) {
	recordTemplateEngineOldEngineRoute();
	return parser().resolveQualifiedOwnerForLookup(owner_name);
}

inline std::optional<TemplateArgumentVector> TemplateEngine::materializeConcreteCallTemplateArguments(
	std::span<const ASTNode> template_argument_nodes) {
	recordTemplateEngineOldEngineRoute();
	return parser().materializeConcreteCallTemplateArguments(template_argument_nodes);
}

inline std::optional<ASTNode> TemplateEngine::resolveDefinitionBoundOrdinaryCall(
	const FunctionCallDefinitionLookupRecord& record,
	std::span<const TypeSpecifierNode> arg_types) {
	recordTemplateEngineOldEngineRoute();
	return parser().resolveDefinitionBoundOrdinaryCall(record, arg_types);
}

inline std::optional<ASTNode> TemplateEngine::resolveDefinitionBoundQualifiedTemplateCall(
	const FunctionCallDefinitionLookupRecord& record,
	std::string_view qualified_name,
	std::span<const ASTNode> template_argument_nodes,
	const ChunkedVector<ASTNode>& arguments,
	std::span<const TypeSpecifierNode> arg_types) {
	recordTemplateEngineOldEngineRoute();
	return parser().resolveDefinitionBoundQualifiedTemplateCall(
		record,
		qualified_name,
		template_argument_nodes,
		arguments,
		arg_types);
}

inline std::optional<ASTNode> TemplateEngine::resolveDeferredQualifiedTemplateCall(
	std::string_view qualified_name,
	std::span<const ASTNode> template_argument_nodes,
	const ChunkedVector<ASTNode>& arguments,
	std::span<const TypeSpecifierNode> arg_types) {
	recordTemplateEngineOldEngineRoute();
	return parser().resolveDeferredQualifiedTemplateCall(
		qualified_name,
		template_argument_nodes,
		arguments,
		arg_types);
}

inline std::optional<ASTNode> TemplateEngine::tryInstantiateTemplateFromCallArguments(
	std::string_view qualified_name,
	std::string_view simple_name,
	const ChunkedVector<ASTNode>& arguments) {
	recordTemplateEngineOldEngineRoute();
	return parser().tryInstantiateTemplateFromCallArguments(
		qualified_name,
		simple_name,
		arguments);
}

inline const FunctionDeclarationNode* TemplateEngine::tryInstantiateOperatorTemplateForBinary(
	std::string_view op_symbol,
	const TypeSpecifierNode& left_type_spec,
	const TypeSpecifierNode& right_type_spec) {
	recordTemplateEngineOldEngineRoute();
	return parser().tryInstantiateOperatorTemplateForBinary(
		op_symbol,
		left_type_spec,
		right_type_spec);
}

inline std::optional<ASTNode> TemplateEngine::instantiateLazyMemberForCanonicalOwner(
	std::string_view& owner_name,
	std::string_view member_name,
	std::span<const TemplateTypeArg> owner_template_args) {
	recordTemplateEngineOldEngineRoute();
	return parser().instantiateLazyMemberForCanonicalOwner(owner_name, member_name, owner_template_args);
}

inline bool TemplateEngine::instantiateLazyClassToPhase(
	StringHandle instantiated_name,
	ClassInstantiationPhase target_phase) {
	recordTemplateEngineOldEngineRoute();
	return parser().instantiateLazyClassToPhase(instantiated_name, target_phase);
}

inline std::optional<TypeIndex> TemplateEngine::instantiateLazyNestedType(
	StringHandle parent_class_name,
	StringHandle nested_type_name) {
	recordTemplateEngineOldEngineRoute();
	return parser().instantiateLazyNestedType(parent_class_name, nested_type_name);
}

inline std::optional<ASTNode> TemplateEngine::instantiateLazyMemberIfNeeded(const LazyMemberKey& member_key) {
	recordTemplateEngineOldEngineRoute();
	return parser().instantiateLazyMemberIfNeeded(member_key);
}

inline bool TemplateEngine::instantiateLazyStaticMember(
	StringHandle instantiated_class_name,
	StringHandle member_name) {
	recordTemplateEngineOldEngineRoute();
	return parser().instantiateLazyStaticMember(instantiated_class_name, member_name);
}

inline std::optional<ASTNode> TemplateEngine::instantiateLazyMemberFunction(
	const LazyMemberFunctionInfo& lazy_info,
	bool materialize_body) {
	recordTemplateEngineOldEngineRoute();
	return parser().instantiateLazyMemberFunction(lazy_info, materialize_body);
}

inline const ConstructorDeclarationNode* TemplateEngine::materializeMatchingConstructorTemplate(
	StringHandle instantiated_struct_name,
	const StructTypeInfo& struct_info,
	std::span<const TypeSpecifierNode> arg_types,
	const ConstructorDeclarationNode* preferred_ctor,
	bool& is_ambiguous) {
	recordTemplateEngineOldEngineRoute();
	return parser().materializeMatchingConstructorTemplate(
		instantiated_struct_name,
		struct_info,
		arg_types,
		preferred_ctor,
		is_ambiguous);
}

inline std::optional<ASTNode> TemplateEngine::parseTemplateBody(
	Parser::SaveHandle body_pos,
	std::span<const std::string_view> template_param_names,
	std::span<const TypeCategory> concrete_types,
	StringHandle struct_name,
	TypeIndex struct_type_index) {
	recordTemplateEngineOldEngineRoute();
	return parser().parseTemplateBody(
		body_pos,
		template_param_names,
		concrete_types,
		struct_name,
		struct_type_index);
}

inline ASTNode TemplateEngine::substituteTemplateParameters(
	const ASTNode& node,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args) {
	recordTemplateEngineOldEngineRoute();
	return parser().substituteTemplateParameters(node, template_params, template_args);
}

inline ASTNode TemplateEngine::substituteTemplateParameters(
	const ASTNode& node,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args,
	TypeIndex current_owner_type_index,
	bool has_implicit_this) {
	recordTemplateEngineOldEngineRoute();
	return parser().substituteTemplateParameters(
		node,
		template_params,
		template_args,
		current_owner_type_index,
		has_implicit_this);
}

inline ASTNode TemplateEngine::substituteTemplateParameters(
	const ASTNode& node,
	const TemplateInstantiationContext& context) {
	recordTemplateEngineOldEngineRoute();
	return parser().substituteTemplateParameters(node, context);
}

inline std::optional<ASTNode> TemplateEngine::instantiateClassTemplateForSignatureReplay(
	std::string_view template_name,
	std::span<const TemplateTypeArg> template_args,
	bool force_eager) {
	recordTemplateEngineOldEngineRoute();
	return parser().instantiateClassTemplateForSignatureReplay(template_name, template_args, force_eager);
}
