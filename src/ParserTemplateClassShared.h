#pragma once

ASTNode rebindStaticMemberInitializerFunctionCalls(
	const ASTNode& node,
	const StructTypeInfo* struct_info
);

template<typename TDest, typename TSource>
void appendLazyTemplateSequence(TDest& destination, const TSource& source)
{
	for (const auto& value : source) {
		destination.push_back(value);
	}
}

template<typename TParams, typename TArgs>
LazyMemberFunctionInfo buildLazyNestedMemberFunctionInfo(
	const StructMemberFunctionDecl& mem_func,
	StringHandle class_template_name,
	StringHandle qualified_name,
	StringHandle member_function_name,
	bool is_constructor,
	bool is_destructor,
	const TParams& template_params,
	const TArgs& template_args)
{
	LazyMemberFunctionInfo lazy_mem_info;
	auto& id = lazy_mem_info.identity;
	id.original_member_node = mem_func.function_declaration;
	id.template_owner_name = class_template_name;
	id.instantiated_owner_name = qualified_name;
	id.original_lookup_name = member_function_name;
	id.operator_kind = mem_func.operator_kind;
	id.is_operator = mem_func.is_operator_overload();
	id.is_const_method = mem_func.is_const();
	id.cv_qualifier = mem_func.cv_qualifier;
	if (is_constructor)
		id.kind = DeferredMemberIdentity::Kind::Constructor;
	else if (is_destructor)
		id.kind = DeferredMemberIdentity::Kind::Destructor;
	else
		id.kind = DeferredMemberIdentity::Kind::Function;
	appendLazyTemplateSequence(lazy_mem_info.template_params, template_params);
	appendLazyTemplateSequence(lazy_mem_info.template_args, template_args);
	lazy_mem_info.access = mem_func.access;
	lazy_mem_info.is_virtual = mem_func.is_virtual;
	lazy_mem_info.is_pure_virtual = mem_func.is_pure_virtual;
	lazy_mem_info.is_override = mem_func.is_override;
	lazy_mem_info.is_final = mem_func.is_final;
	return lazy_mem_info;
}

template<typename TParams, typename TArgs>
void registerNestedMemberFunctionsForLazy(
	const StructDeclarationNode& nested_struct,
	StructTypeInfo& nested_struct_info,
	StringHandle class_template_name,
	StringHandle qualified_name,
	const TParams& template_params,
	const TArgs& template_args)
{
	for (const StructMemberFunctionDecl& mem_func : nested_struct.member_functions()) {
		if (mem_func.is_constructor || mem_func.is_destructor) {
			if (mem_func.is_constructor)
				nested_struct_info.addConstructor(mem_func.function_declaration, mem_func.access);
			else
				nested_struct_info.addDestructor(mem_func.function_declaration, mem_func.access, mem_func.is_virtual);

			StringHandle member_function_name{};
			if (mem_func.function_declaration.is<ConstructorDeclarationNode>()) {
				member_function_name = mem_func.function_declaration.as<ConstructorDeclarationNode>().name();
			} else if (mem_func.function_declaration.is<DestructorDeclarationNode>()) {
				member_function_name = mem_func.function_declaration.as<DestructorDeclarationNode>().name();
			}

			auto lazy_mem_info = buildLazyNestedMemberFunctionInfo(
				mem_func,
				class_template_name,
				qualified_name,
				member_function_name,
				mem_func.is_constructor,
				mem_func.is_destructor,
				template_params,
				template_args
			);
			LazyMemberInstantiationRegistry::getInstance().registerLazyMember(std::move(lazy_mem_info));
		} else if (mem_func.function_declaration.is<FunctionDeclarationNode>()) {
			const FunctionDeclarationNode& func_decl = mem_func.function_declaration.as<FunctionDeclarationNode>();
			const DeclarationNode& decl = func_decl.decl_node();

			auto lazy_mem_info = buildLazyNestedMemberFunctionInfo(
				mem_func,
				class_template_name,
				qualified_name,
				decl.identifier_token().handle(),
				false,
				false,
				template_params,
				template_args
			);

			LazyMemberInstantiationRegistry::getInstance().registerLazyMember(std::move(lazy_mem_info));

			// Set is_const/volatile_member_function on the node so propagateAstProperties derives cv_qualifier.
			{
				ASTNode fn_node = mem_func.function_declaration;
				if (auto* fn = get_function_decl_node_mut(fn_node)) {
					fn->set_is_const_member_function(mem_func.is_const());
					fn->set_is_volatile_member_function(mem_func.is_volatile());
				}
			}
			nested_struct_info.addMemberFunction(
				decl.identifier_token().handle(),
				mem_func.function_declaration,
				mem_func.access,
				mem_func.is_virtual,
				mem_func.is_pure_virtual,
				mem_func.is_override,
				mem_func.is_final
			);
			// cv_qualifier is now auto-derived by propagateAstProperties

			FLASH_LOG(Templates, Debug, "Registered lazy member function for nested type: ",
				qualified_name, "::", decl.identifier_token().value());
		}
	}
}
