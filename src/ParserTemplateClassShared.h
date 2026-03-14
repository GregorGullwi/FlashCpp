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
	lazy_mem_info.class_template_name = class_template_name;
	lazy_mem_info.instantiated_class_name = qualified_name;
	lazy_mem_info.member_function_name = member_function_name;
	lazy_mem_info.original_function_node = mem_func.function_declaration;
	appendLazyTemplateSequence(lazy_mem_info.template_params, template_params);
	appendLazyTemplateSequence(lazy_mem_info.template_args, template_args);
	lazy_mem_info.access = mem_func.access;
	lazy_mem_info.is_virtual = mem_func.is_virtual;
	lazy_mem_info.is_pure_virtual = mem_func.is_pure_virtual;
	lazy_mem_info.is_override = mem_func.is_override;
	lazy_mem_info.is_final = mem_func.is_final;
	lazy_mem_info.is_const_method = mem_func.is_const();
	lazy_mem_info.is_constructor = is_constructor;
	lazy_mem_info.is_destructor = is_destructor;
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

			nested_struct_info.addMemberFunction(
				decl.identifier_token().handle(),
				mem_func.function_declaration,
				mem_func.access,
				mem_func.is_virtual,
				mem_func.is_pure_virtual,
				mem_func.is_override,
				mem_func.is_final
			);
			if (!nested_struct_info.member_functions.empty())
				nested_struct_info.member_functions.back().cv_qualifier = mem_func.cv_qualifier;

			FLASH_LOG(Templates, Debug, "Registered lazy member function for nested type: ",
				qualified_name, "::", decl.identifier_token().value());
		}
	}
}
