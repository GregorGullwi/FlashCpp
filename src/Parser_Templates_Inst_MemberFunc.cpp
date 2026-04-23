#include "Parser.h"
#include "ConstExprEvaluator.h"
#include "NameMangling.h"
#include "OverloadResolution.h"
#include "TypeTraitEvaluator.h"

namespace {
std::string_view unqualifiedTypeComponent(std::string_view type_name) {
	size_t scope_pos = type_name.rfind("::");
	return scope_pos == std::string_view::npos ? type_name : type_name.substr(scope_pos + 2);
}

std::optional<StringHandle> getTemplateLookupOwnerName(std::string_view struct_name) {
	auto type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(struct_name));
	if (type_it == getTypesByNameMap().end()) {
		return std::nullopt;
	}

	const TypeInfo* type_info = type_it->second;
	if (!type_info->struct_info_) {
		if (type_info->isTemplateInstantiation()) {
			return type_info->baseTemplateName();
		}
		return std::nullopt;
	}

	StringBuilder owner_name_builder;
	bool has_owner_component = false;
	auto append_lookup_owner = [&](const auto& self, const StructTypeInfo* current_struct) -> void {
		if (current_struct == nullptr) {
			return;
		}

		if (const StructTypeInfo* enclosing = current_struct->getEnclosingClass()) {
			self(self, enclosing);
			if (has_owner_component) {
				owner_name_builder.append("::"sv);
			}
			owner_name_builder.append(unqualifiedTypeComponent(StringTable::getStringView(current_struct->getName())));
			has_owner_component = true;
			return;
		}

		std::string_view current_name = StringTable::getStringView(current_struct->getName());
		auto current_type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(current_name));
		if (current_type_it != getTypesByNameMap().end() && current_type_it->second->isTemplateInstantiation()) {
			owner_name_builder.append(StringTable::getStringView(current_type_it->second->baseTemplateName()));
		} else {
			owner_name_builder.append(current_name);
		}
		has_owner_component = true;
	};

	append_lookup_owner(append_lookup_owner, type_info->struct_info_.get());
	if (!has_owner_component) {
		return std::nullopt;
	}
	return StringTable::getOrInternStringHandle(owner_name_builder.commit());
}
}

bool Parser::tryAppendMemberDefaultTemplateArg(
	const TemplateParameterNode& param,
	const std::vector<ASTNode>& template_params,
	const OuterTemplateBinding* outer_binding,
	InlineVector<TemplateTypeArg, 4>& current_template_args) {
	// Member templates don't have a separate namespace context - use invalid handle
	// which causes no namespace scope to be entered during SFINAE reparse.
	if (tryAppendDefaultTemplateArg(param, template_params, current_template_args, NamespaceHandle{})) {
		return true;
	}
	if (!param.has_default() || !outer_binding) {
		return false;
	}

	InlineVector<ASTNode, 4> combined_template_params;
	InlineVector<TemplateTypeArg, 4> combined_template_args;
	combined_template_params.reserve(outer_binding->param_names.size() + template_params.size());
	combined_template_args.reserve(outer_binding->param_args.size() + current_template_args.size());
	if (outer_binding->param_names.size() != outer_binding->param_args.size()) {
		throw InternalError("Outer template binding parameter state is inconsistent");
	}

	for (size_t i = 0; i < outer_binding->param_names.size(); ++i) {
		StringHandle outer_name = outer_binding->param_names[i];
		const TemplateTypeArg& outer_arg = outer_binding->param_args[i];
		Token outer_token(Token::Type::Identifier, StringTable::getStringView(outer_name), 0, 0, 0);
		if (outer_arg.is_value) {
			auto outer_type_node = emplace_node<TypeSpecifierNode>(
				outer_arg.type_index.withCategory(outer_arg.typeEnum()),
				get_type_size_bits(outer_arg.typeEnum()),
				outer_token,
				CVQualifier::None,
				ReferenceQualifier::None);
			combined_template_params.push_back(emplace_node<TemplateParameterNode>(outer_name, outer_type_node, outer_token));
		} else if (outer_arg.is_template_template_arg) {
			combined_template_params.push_back(
				emplace_node<TemplateParameterNode>(outer_name, std::vector<ASTNode>{}, outer_token));
		} else {
			auto outer_param = emplace_node<TemplateParameterNode>(outer_name, outer_token);
			outer_param.as<TemplateParameterNode>().set_registered_type_index(
				outer_arg.type_index.withCategory(outer_arg.typeEnum()));
			combined_template_params.push_back(outer_param);
		}
		combined_template_args.push_back(outer_arg);
	}

	for (const auto& template_param_node : template_params) {
		combined_template_params.push_back(template_param_node);
	}
	for (const auto& current_arg : current_template_args) {
		combined_template_args.push_back(current_arg);
	}

	ASTNode substituted_default = substituteTemplateParameters(
		param.default_value(),
		combined_template_params,
		combined_template_args);
	if (param.kind() == TemplateParameterKind::Type && substituted_default.is<TypeSpecifierNode>()) {
		current_template_args.push_back(TemplateTypeArg(substituted_default.as<TypeSpecifierNode>()));
		return true;
	}
	if (param.kind() == TemplateParameterKind::NonType && substituted_default.is<ExpressionNode>()) {
		ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
		eval_ctx.parser = this;
		auto eval_result = ConstExpr::Evaluator::evaluate(substituted_default, eval_ctx);
		if (eval_result.success()) {
			current_template_args.push_back(templateTypeArgFromEvalResult(eval_result));
			return true;
		}
	}
	return false;
}

std::optional<ASTNode> Parser::try_instantiate_member_function_template(
	std::string_view struct_name,
	std::string_view member_name,
	const std::vector<TypeSpecifierNode>& arg_types) {

	// Build the qualified template name
	StringBuilder qualified_name_sb;
	qualified_name_sb.append(struct_name).append("::").append(member_name);
	StringHandle qualified_name = StringTable::getOrInternStringHandle(qualified_name_sb);

	// Push a parser-level instantiation context for provenance tracking and backtraces.
	ScopedParserInstantiationContext inst_ctx_guard(*this, template_instantiation_mode_, qualified_name);

	// Look up the template in the registry
	auto template_opt = gTemplateRegistry.lookupTemplate(qualified_name);

	// If not found, recover the source owner name for instantiated/nested owners
	// (e.g. Outer$hash::Inner -> Outer::Inner, math::Adder$hash -> math::Adder).
	if (!template_opt.has_value()) {
		if (auto lookup_owner = getTemplateLookupOwnerName(struct_name)) {
			StringBuilder base_qualified_name_sb;
			base_qualified_name_sb.append(StringTable::getStringView(*lookup_owner)).append("::").append(member_name);
			StringHandle base_qualified_name = StringTable::getOrInternStringHandle(base_qualified_name_sb);
			if (base_qualified_name != qualified_name) {
				template_opt = gTemplateRegistry.lookupTemplate(base_qualified_name);
				if (template_opt.has_value()) {
					qualified_name = base_qualified_name;
				}
			}
		}
	}

	if (!template_opt.has_value()) {
		return std::nullopt;	 // Not a template
	}

	const ASTNode& template_node = *template_opt;
	if (!template_node.is<TemplateFunctionDeclarationNode>()) {
		return std::nullopt;	 // Not a function template
	}

	const TemplateFunctionDeclarationNode& template_func = template_node.as<TemplateFunctionDeclarationNode>();
	const FunctionDeclarationNode& func_decl = template_func.function_decl_node();
	const auto& template_params = template_func.template_parameters();
	const OuterTemplateBinding* outer_binding = gTemplateRegistry.getOuterTemplateBinding(qualified_name.view());
	if (arg_types.empty()) {
		return std::nullopt;	 // Can't deduce without arguments
	}

	std::vector<TemplateTypeArg> template_args;
	auto deduction_info = buildDeductionMapFromCallArgs(
		template_params,
		func_decl,
		arg_types,
		0);
	if (!deduction_info.has_value()) {
		return std::nullopt;
	}

	size_t arg_index = 0;
	for (const auto& template_param_node : template_params) {
		const TemplateParameterNode& param = template_param_node.as<TemplateParameterNode>();

		if (param.kind() == TemplateParameterKind::Template) {
			return std::nullopt;
		}

		if (param.kind() != TemplateParameterKind::Type) {
			return std::nullopt;
		}

		// Variadic type parameter: consume the function-parameter-pack call-arg
		// slice (if this template pack maps to the function-parameter pack) or
		// produce an empty pack (if it does not, e.g. a separate template-level
		// pack that has no function-parameter counterpart).  This mirrors the
		// variadic-Type branch in deduceTemplateArgsFromCall.
		if (param.is_variadic()) {
			if (!deduction_info->function_pack_dependent_param_names.count(param.nameHandle())) {
				continue;  // empty pack
			}
			size_t start = deduction_info->function_pack_call_arg_start;
			size_t end = deduction_info->function_pack_call_arg_end;
			if (start == SIZE_MAX || end == SIZE_MAX || start > end) {
				continue;  // no function-parameter pack slice available
			}
			for (size_t i = start; i < end; ++i) {
				if (deduction_info->pre_deduced_arg_indices.count(i)) {
					continue;
				}
				const TypeSpecifierNode& ca_type = arg_types[i];
				bool pushed = false;
				if (auto extracted_arg = extractNestedTemplateArgForDependentName(
						deduction_info->function_pack_element_type_index,
						ca_type.type_index(),
						param.nameHandle())) {
					template_args.push_back(*extracted_arg);
					pushed = true;
				}
				if (!pushed) {
					template_args.push_back(TemplateTypeArg::makeType(
						ca_type.type_index().withCategory(ca_type.type())));
				}
			}
			arg_index = end;
			continue;
		}

		auto deduced_it = deduction_info->param_name_to_arg.find(param.nameHandle());
		if (deduced_it != deduction_info->param_name_to_arg.end()) {
			template_args.push_back(deduced_it->second);
			continue;
		}

		while (arg_index < arg_types.size() &&
			   deduction_info->pre_deduced_arg_indices.count(arg_index)) {
			++arg_index;
		}

		if (arg_index < arg_types.size()) {
			template_args.push_back(TemplateTypeArg::makeType(
				arg_types[arg_index].type_index().withCategory(arg_types[arg_index].type())));
			++arg_index;
		} else {
			InlineVector<TemplateTypeArg, 4> default_args;
			for (const auto& existing_arg : template_args) {
				default_args.push_back(existing_arg);
			}
			if (!tryAppendMemberDefaultTemplateArg(param, template_params, outer_binding, default_args)) {
				return std::nullopt;
			}
			template_args.push_back(default_args.back());
		}
	}

	// Check if we already have this instantiation
	auto key = FlashCpp::makeInstantiationKey(qualified_name, template_args);

	auto existing_inst = gTemplateRegistry.getInstantiation(key);
	if (existing_inst.has_value()) {
		return *existing_inst;  // Return existing instantiation
	}

	return instantiate_member_function_template_core(
		struct_name, member_name, qualified_name, template_node, template_args, key, arg_types);
}

std::optional<ASTNode> Parser::try_instantiate_constructor_template(
	StringHandle instantiated_struct_name,
	const ConstructorDeclarationNode& ctor_decl,
	const std::vector<TypeSpecifierNode>& arg_types) {
	const auto& template_params = ctor_decl.template_parameters();
	if (template_params.empty()) {
		return std::nullopt;
	}

	auto deduction_info = buildDeductionMapFromCallArgs(
		template_params,
		ctor_decl.parameter_nodes(),
		arg_types,
		0);
	if (!deduction_info.has_value()) {
		return std::nullopt;
	}

	InlineVector<TemplateTypeArg, 4> ctor_template_args;
	size_t arg_index = 0;
	for (const auto& template_param_node : template_params) {
		if (!template_param_node.is<TemplateParameterNode>()) {
			return std::nullopt;
		}
		const auto& param = template_param_node.as<TemplateParameterNode>();
		auto deduced_it = deduction_info->param_name_to_arg.find(param.nameHandle());
		if (deduced_it != deduction_info->param_name_to_arg.end()) {
			ctor_template_args.push_back(deduced_it->second);
			continue;
		}

		while (arg_index < arg_types.size() &&
			   deduction_info->pre_deduced_arg_indices.count(arg_index)) {
			++arg_index;
		}

		if (param.kind() == TemplateParameterKind::Type && arg_index < arg_types.size()) {
			ctor_template_args.push_back(TemplateTypeArg::makeType(
				arg_types[arg_index].type_index().withCategory(arg_types[arg_index].type())));
			++arg_index;
			continue;
		}

		// Constructor templates don't have a separate namespace context
		if (!tryAppendDefaultTemplateArg(param, template_params, ctor_template_args, NamespaceHandle{})) {
			return std::nullopt;
		}
	}

	LazyMemberFunctionInfo lazy_info;
	lazy_info.identity.original_member_node = emplace_node<ConstructorDeclarationNode>(ctor_decl);
	lazy_info.identity.template_owner_name = instantiated_struct_name;
	lazy_info.identity.instantiated_owner_name = instantiated_struct_name;
	lazy_info.identity.original_lookup_name = ctor_decl.name();
	lazy_info.identity.kind = DeferredMemberIdentity::Kind::Constructor;
	lazy_info.identity.is_const_method = false;

	for (StringHandle outer_name : ctor_decl.outer_template_param_names()) {
		Token outer_token(Token::Type::Identifier, StringTable::getStringView(outer_name), 0, 0, 0);
		lazy_info.template_params.push_back(emplace_node<TemplateParameterNode>(outer_name, outer_token));
	}
	for (const auto& outer_arg : ctor_decl.outer_template_args()) {
		const std::vector<ASTNode> no_params;
		const std::vector<TemplateTypeArg> no_args;
		lazy_info.template_args.push_back(materializeTemplateArg(
			outer_arg,
			no_params,
			no_args,
			nullptr));
	}
	for (const auto& template_param : template_params) {
		lazy_info.template_params.push_back(template_param);
	}
	for (const auto& template_arg : ctor_template_args) {
		lazy_info.template_args.push_back(template_arg);
	}

	return instantiateLazyMemberFunction(lazy_info);
}

const ConstructorDeclarationNode* Parser::materializeMatchingConstructorTemplate(
	StringHandle instantiated_struct_name,
	const StructTypeInfo& struct_info,
	const std::vector<TypeSpecifierNode>& arg_types,
	const ConstructorDeclarationNode* preferred_ctor,
	bool& is_ambiguous) {
	is_ambiguous = false;

	auto findExistingMaterializedCtor = [&](std::string_view mangled_name) -> const ConstructorDeclarationNode* {
		if (mangled_name.empty()) {
			return nullptr;
		}
		for (const auto& member_func : struct_info.member_functions) {
			if (!member_func.is_constructor || !member_func.function_decl.is<ConstructorDeclarationNode>()) {
				continue;
			}
			const auto& ctor = member_func.function_decl.as<ConstructorDeclarationNode>();
			if (ctor.has_template_parameters() || !ctor.is_materialized()) {
				continue;
			}
			if (ctor.mangled_name() == mangled_name) {
				return &ctor;
			}
		}
		return nullptr;
	};

	auto attachInstantiatedCtor = [&](const ConstructorDeclarationNode& source_ctor, ASTNode instantiated_node) -> const ConstructorDeclarationNode* {
		if (!instantiated_node.is<ConstructorDeclarationNode>()) {
			return nullptr;
		}
		auto& instantiated_ctor = instantiated_node.as<ConstructorDeclarationNode>();
		if (const ConstructorDeclarationNode* existing_ctor = findExistingMaterializedCtor(instantiated_ctor.mangled_name())) {
			return existing_ctor;
		}

		AccessSpecifier ctor_access = AccessSpecifier::Public;
		for (const auto& member_func : struct_info.member_functions) {
			if (!member_func.is_constructor || !member_func.function_decl.is<ConstructorDeclarationNode>()) {
				continue;
			}
			if (member_func.function_decl.raw_pointer() != static_cast<const void*>(&source_ctor)) {
				continue;
			}
			ctor_access = member_func.access;
			break;
		}

		if (auto type_it = getTypesByNameMap().find(instantiated_struct_name);
			type_it != getTypesByNameMap().end()) {
			if (StructTypeInfo* mutable_struct_info = type_it->second->getStructInfo()) {
				mutable_struct_info->addConstructor(instantiated_node, ctor_access);
			}
		}

		if (auto struct_root = lookupLateMaterializedOwningStructRoot(instantiated_struct_name);
			struct_root.has_value() && struct_root->is<StructDeclarationNode>()) {
			auto& struct_decl = struct_root->as<StructDeclarationNode>();
			bool already_present = false;
			for (const auto& member_func : struct_decl.member_functions()) {
				if (!member_func.is_constructor || !member_func.function_declaration.is<ConstructorDeclarationNode>()) {
					continue;
				}
				const auto& existing_ctor = member_func.function_declaration.as<ConstructorDeclarationNode>();
				if (existing_ctor.mangled_name() == instantiated_ctor.mangled_name()) {
					already_present = true;
					break;
				}
			}
			if (!already_present) {
				struct_decl.add_constructor(instantiated_node, ctor_access);
			}
		}

		registerLateMaterializedOwningStructRoot(instantiated_struct_name);
		normalizePendingSemanticRootsIfAvailable();
		return &instantiated_ctor;
	};

	auto matches_call_arguments = [&](const ConstructorDeclarationNode& ctor) {
		size_t min_required = countMinRequiredArgs(ctor);
		if (arg_types.size() < min_required || arg_types.size() > ctor.parameter_nodes().size()) {
			return false;
		}
		for (size_t i = 0; i < arg_types.size(); ++i) {
			if (!ctor.parameter_nodes()[i].is<DeclarationNode>()) {
				return false;
			}
			const auto& param_type = ctor.parameter_nodes()[i].as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
			if (!can_convert_type(arg_types[i], param_type).is_valid) {
				return false;
			}
		}
		return true;
	};

	if (preferred_ctor != nullptr) {
		if (!preferred_ctor->has_template_parameters()) {
			return preferred_ctor;
		}
		auto instantiated = try_instantiate_constructor_template(
			instantiated_struct_name,
			*preferred_ctor,
			arg_types);
		if (instantiated.has_value() && instantiated->is<ConstructorDeclarationNode>()) {
			const ConstructorDeclarationNode* concrete_ctor = attachInstantiatedCtor(*preferred_ctor, *instantiated);
			if (concrete_ctor && matches_call_arguments(*concrete_ctor)) {
				return concrete_ctor;
			}
		}
		// Instantiation failed or the instantiated ctor doesn't match call arguments.
		// Return nullptr so callers fall back to arity-based resolution instead of
		// forwarding an uninstantiated template constructor.
		return nullptr;
	}

	const ConstructorDeclarationNode* instantiated_match = nullptr;
	for (const auto& member_func : struct_info.member_functions) {
		if (!member_func.is_constructor || !member_func.function_decl.is<ConstructorDeclarationNode>()) {
			continue;
		}
		const auto& ctor_decl = member_func.function_decl.as<ConstructorDeclarationNode>();
		if (!ctor_decl.has_template_parameters()) {
			continue;
		}
		auto instantiated = try_instantiate_constructor_template(
			instantiated_struct_name,
			ctor_decl,
			arg_types);
		if (!instantiated.has_value() || !instantiated->is<ConstructorDeclarationNode>()) {
			continue;
		}
		const ConstructorDeclarationNode* concrete_ctor = attachInstantiatedCtor(ctor_decl, *instantiated);
		if (!concrete_ctor || !matches_call_arguments(*concrete_ctor)) {
			continue;
		}
		if (instantiated_match != nullptr) {
			is_ambiguous = true;
			return nullptr;
		}
		instantiated_match = concrete_ctor;
	}

	return instantiated_match;
}

// Instantiate member function template with explicit template arguments
// Example: obj.convert<int>(42)
std::optional<ASTNode> Parser::try_instantiate_member_function_template_explicit(
	std::string_view struct_name,
	std::string_view member_name,
	const std::vector<TemplateTypeArg>& template_type_args) {

	// Build the qualified template name using StringBuilder
	StringBuilder qualified_name_sb;
	qualified_name_sb.append(struct_name).append("::").append(member_name);
	StringHandle qualified_name = StringTable::getOrInternStringHandle(qualified_name_sb);
	StringHandle specialization_lookup_name = qualified_name;
	StringHandle struct_name_handle = StringTable::getOrInternStringHandle(struct_name);
	auto requested_key = FlashCpp::makeInstantiationKey(qualified_name, template_type_args);
	if (auto existing_inst = gTemplateRegistry.getInstantiation(requested_key);
		existing_inst.has_value()) {
		return *existing_inst;
	}
	TypeInfo* struct_type_info = nullptr;
	if (auto struct_type_it = getTypesByNameMap().find(struct_name_handle);
		struct_type_it != getTypesByNameMap().end()) {
		struct_type_info = struct_type_it->second;
	}

	auto build_member_lookup_name = [&](std::string_view class_name) {
		StringBuilder lookup_name_sb;
		lookup_name_sb.append(class_name).append("::").append(member_name);
		return StringTable::getOrInternStringHandle(lookup_name_sb.commit());
	};

	// FIRST: Check if we have an explicit specialization for these template arguments
	auto specialization_opt = gTemplateRegistry.lookupSpecialization(
		specialization_lookup_name.view(),
		template_type_args);
	if (!specialization_opt.has_value()) {
		std::string_view qualified_base_class_name;
		if (struct_type_info && struct_type_info->isTemplateInstantiation()) {
			qualified_base_class_name = buildQualifiedNameFromHandle(
				struct_type_info->sourceNamespace(),
				StringTable::getStringView(struct_type_info->baseTemplateName()));
		}
		if (!qualified_base_class_name.empty()) {
			specialization_lookup_name = build_member_lookup_name(qualified_base_class_name);
			specialization_opt = gTemplateRegistry.lookupSpecialization(
				specialization_lookup_name.view(),
				template_type_args);
		}
		if (!specialization_opt.has_value()) {
			std::string_view base_class_name = extractBaseTemplateName(struct_name);
			if (!base_class_name.empty() && base_class_name != qualified_base_class_name) {
				specialization_lookup_name = build_member_lookup_name(base_class_name);
				specialization_opt = gTemplateRegistry.lookupSpecialization(
					specialization_lookup_name.view(),
					template_type_args);
			}
		}
	}
	if (specialization_opt.has_value()) {
		FLASH_LOG(Templates, Debug, "Found explicit specialization for ", specialization_lookup_name.view());
		// We have an explicit specialization - parse its body if needed
		ASTNode& spec_node = *specialization_opt;
		if (spec_node.is<FunctionDeclarationNode>()) {
			FunctionDeclarationNode& spec_func = spec_node.as<FunctionDeclarationNode>();

			// If the specialization has a body position and no definition yet, parse it now
			if (spec_func.has_template_body_position() && spec_func.needs_body_materialization()) {
				FLASH_LOG(Templates, Debug, "Parsing specialization body for ", specialization_lookup_name.view());

				// Look up the struct type index and node for the member function context
				TypeIndex struct_type_index{};
				StructDeclarationNode* struct_node_ptr = nullptr;
				auto struct_type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(struct_name));
				if (struct_type_it != getTypesByNameMap().end()) {
					struct_type_index = struct_type_it->second->type_index_;

					// Try to find the struct node in the symbol table
					auto struct_symbol_opt = lookup_symbol(StringTable::getOrInternStringHandle(struct_name));
					if (struct_symbol_opt.has_value() && struct_symbol_opt->is<StructDeclarationNode>()) {
						struct_node_ptr = &struct_symbol_opt->as<StructDeclarationNode>();
					}
				}

				// Save the current position
				SaveHandle saved_pos = save_token_position();

				// Enter a function scope
				gSymbolTable.enter_scope(ScopeType::Function);

				// Set up member function context
				member_function_context_stack_.push_back({
					StringTable::getOrInternStringHandle(struct_name),
					struct_type_index,
					struct_node_ptr,
					nullptr	// local_struct_info - not needed for specialization functions
				});

				// Add parameters to symbol table
				for (const auto& param : spec_func.parameter_nodes()) {
					if (param.is<DeclarationNode>()) {
						const auto& param_decl = param.as<DeclarationNode>();
						gSymbolTable.insert(param_decl.identifier_token().value(), param);
					}
				}

				// Restore to the body position
				restore_lexer_position_only(spec_func.template_body_position());

				// Parse the function body (handles function-try-blocks too)
				auto body_result = parse_function_body();

				// Clean up member function context
				if (!member_function_context_stack_.empty()) {
					member_function_context_stack_.pop_back();
				}

				// Exit the function scope
				gSymbolTable.exit_scope();

				// Restore the original position
				restore_lexer_position_only(saved_pos);

				if (body_result.is_error() || !body_result.node().has_value()) {
					FLASH_LOG(Templates, Error, "Failed to parse specialization body: ", body_result.error_message());
				} else {
					spec_func.set_definition(*body_result.node());
					finalize_function_after_definition(spec_func, true);
					FLASH_LOG(Templates, Debug, "Successfully parsed specialization body");
				}
			}

			const DeclarationNode& spec_decl = spec_func.decl_node();
			std::string_view mangled_name = gTemplateRegistry.mangleTemplateName(member_name, template_type_args);
			Token mangled_token(Token::Type::Identifier, mangled_name,
								spec_decl.identifier_token().line(), spec_decl.identifier_token().column(),
								spec_decl.identifier_token().file_index());
			auto [inst_decl_node, inst_decl_ref] = emplace_node_ref<DeclarationNode>(
				spec_decl.type_node(),
				mangled_token);
			auto [inst_func_node, inst_func_ref] = emplace_node_ref<FunctionDeclarationNode>(
				inst_decl_ref,
				struct_name);
			copy_function_properties(inst_func_ref, spec_func);
			for (const auto& param : spec_func.parameter_nodes()) {
				inst_func_ref.add_parameter_node(param);
			}
			if (spec_func.has_non_type_template_args()) {
				inst_func_ref.set_non_type_template_args(spec_func.non_type_template_args());
			}
			if (spec_func.is_materialized()) {
				inst_func_ref.set_definition(*spec_func.get_definition());
				finalize_function_after_definition(inst_func_ref, true);
			} else {
				compute_and_set_mangled_name(inst_func_ref, true);
			}
			registerAndNormalizeLateMaterializedTopLevelNode(inst_func_node);
			gTemplateRegistry.registerInstantiation(requested_key, inst_func_node);
			return inst_func_node;
		}
	}

	// Look up ALL template overloads in the registry for SFINAE support
	const std::vector<ASTNode>* all_templates = gTemplateRegistry.lookupAllTemplates(qualified_name.view());

	// If not found and struct_name looks like an instantiated template (e.g., has_foo$a1b2c3),
	// try the base template class name (e.g., has_foo::method)
	if (!all_templates || all_templates->empty()) {
		std::string_view qualified_base_class_name;
		if (struct_type_info && struct_type_info->isTemplateInstantiation()) {
			qualified_base_class_name = buildQualifiedNameFromHandle(
				struct_type_info->sourceNamespace(),
				StringTable::getStringView(struct_type_info->baseTemplateName()));
		}
		if (!qualified_base_class_name.empty()) {
			StringHandle base_qualified_name = build_member_lookup_name(qualified_base_class_name);
			all_templates = gTemplateRegistry.lookupAllTemplates(base_qualified_name.view());
			FLASH_LOG(Templates, Debug, "Trying base template class lookup: ", base_qualified_name.view());
		}
		if (!all_templates || all_templates->empty()) {
			std::string_view base_class_name = extractBaseTemplateName(struct_name);
			if (!base_class_name.empty() && base_class_name != qualified_base_class_name) {
				StringHandle base_qualified_name = build_member_lookup_name(base_class_name);
				all_templates = gTemplateRegistry.lookupAllTemplates(base_qualified_name.view());
				FLASH_LOG(Templates, Debug, "Trying base template class lookup: ", base_qualified_name.view());
			}
		}
	}

	if (!all_templates || all_templates->empty()) {
		return std::nullopt;	 // Not a template
	}

	// Loop over all overloads for SFINAE support
	for (const auto& template_node : *all_templates) {
		if (!template_node.is<TemplateFunctionDeclarationNode>()) {
			continue;  // Not a function template
		}

		const TemplateFunctionDeclarationNode& template_func = template_node.as<TemplateFunctionDeclarationNode>();
		const auto& template_params = template_func.template_parameters();
		const FunctionDeclarationNode& func_decl = template_func.function_decl_node();
		const OuterTemplateBinding* outer_binding = gTemplateRegistry.getOuterTemplateBinding(qualified_name.view());
		if (template_type_args.size() > template_params.size()) {
			continue;
		}

		InlineVector<TemplateTypeArg, 4> completed_template_args;
		for (const auto& arg : template_type_args) {
			completed_template_args.push_back(arg);
		}
		bool has_all_template_args = true;
		for (size_t i = completed_template_args.size(); i < template_params.size(); ++i) {
			if (!template_params[i].is<TemplateParameterNode>()) {
				has_all_template_args = false;
				break;
			}
			const auto& template_param = template_params[i].as<TemplateParameterNode>();
			if (!tryAppendMemberDefaultTemplateArg(template_param, template_params, outer_binding, completed_template_args)) {
				has_all_template_args = false;
				break;
			}
		}
		if (!has_all_template_args) {
			continue;
		}
		const auto& template_args = completed_template_args;
		auto key = FlashCpp::makeInstantiationKey(qualified_name, template_args);

		// Check if we already have this instantiation
		auto existing_inst = gTemplateRegistry.getInstantiation(key);
		if (existing_inst.has_value()) {
			return *existing_inst;  // Return existing instantiation
		}

		// SFINAE for trailing return type: always re-parse when trailing position is available
		if (func_decl.has_trailing_return_type_position()) {
			FlashCpp::ScopedState guard_ptb(parsing_template_depth_);
			FlashCpp::ScopedState guard_param_names(currentTemplateParamState());
			FlashCpp::ScopedState guard_sfinae_map(sfinae_type_map_);
			ScopedParserInstantiationContext guard_instantiation_mode(*this, TemplateInstantiationMode::SfinaeProbe, StringHandle{});
			parsing_template_depth_ = 0;	 // suppress template body context during SFINAE
			clearCurrentTemplateParameters();  // No dependent names during SFINAE
			sfinae_type_map_.clear();

			SaveHandle sfinae_pos = save_token_position();
			restore_lexer_position_only(func_decl.trailing_return_type_position());
			advance();  // consume '->'

			// Register function parameters so they're visible in decltype expressions
			gSymbolTable.enter_scope(ScopeType::Function);
			register_parameters_in_scope(func_decl.parameter_nodes());

			FlashCpp::TemplateParameterScope sfinae_scope;
			// Add inner template params (the member function template's own params, e.g. U)
			registerTypeParamsInScope(template_params, template_args, sfinae_scope, &sfinae_type_map_);
			// Add outer template params (from enclosing class template, e.g. T→int)
			if (outer_binding)
				registerOuterBindingInScope(*outer_binding, sfinae_scope, &sfinae_type_map_);

			auto return_type_result = parse_type_specifier();
			gSymbolTable.exit_scope();
			restore_lexer_position_only(sfinae_pos);
			// guard_ptb, guard_param_names, guard_sfinae_map, and guard_instantiation_mode
			// restore their fields automatically

			if (return_type_result.is_error() || !return_type_result.node().has_value()) {
				continue;  // SFINAE: this overload's return type failed, try next
			}
		}

		const std::vector<TypeSpecifierNode> empty_call_arg_types;
		const std::vector<TypeSpecifierNode>& call_arg_types =
			current_explicit_call_arg_types_ != nullptr
				? *current_explicit_call_arg_types_
				: empty_call_arg_types;
		auto result = instantiate_member_function_template_core(
			struct_name, member_name, qualified_name, template_node, template_args, key, call_arg_types);
		if (result.has_value()) {
			return result;
		}
	}

	return std::nullopt;
}

std::optional<ASTNode> Parser::instantiate_member_function_template_core(
	std::string_view struct_name, std::string_view member_name,
	StringHandle qualified_name,
	const ASTNode& template_node,
	const std::vector<TemplateTypeArg>& template_args,
	const FlashCpp::TemplateInstantiationKey& key,
	const std::vector<TypeSpecifierNode>& call_arg_types) {

	const TemplateFunctionDeclarationNode& template_func = template_node.as<TemplateFunctionDeclarationNode>();
	const auto& template_params = template_func.template_parameters();
	const FunctionDeclarationNode& func_decl = template_func.function_decl_node();
	const OuterTemplateBinding* outer_binding = gTemplateRegistry.getOuterTemplateBinding(qualified_name.view());
	auto deduction_info = buildDeductionMapFromCallArgs(
		template_params,
		func_decl,
		call_arg_types,
		0);

	// Generate mangled name for the instantiation
	std::string_view mangled_name = gTemplateRegistry.mangleTemplateName(member_name, template_args);

	// Get the original function's declaration
	const DeclarationNode& orig_decl = func_decl.decl_node();

	// Resolved template type: the concrete TypeIndex plus the matching TemplateTypeArg (if any).
	struct ResolvedTemplateType {
		TypeIndex type_index;
		const TemplateTypeArg* arg;	// non-null when the type was resolved via a template parameter
	};

	// Resolves a type against both inner and outer template parameters.
	// TypeIndex already carries the TypeCategory via category(), so only one parameter is needed.
	// Also tracks which inner template parameter index corresponds to each auto parameter
	// so that we know which template argument supplies the concrete type for each auto param.
	size_t auto_param_index = 0;
	auto resolve_template_type = [&](TypeIndex type_index) -> ResolvedTemplateType {
		if (type_index.category() == TypeCategory::Auto) {
			// Abbreviated function template parameter (concept auto / auto):
			// Map this to the corresponding inner template parameter's argument type.
			// Inner template params for auto are named _T0, _T1, etc.
			if (auto_param_index < template_args.size()) {
				const auto& arg = template_args[auto_param_index];
				auto_param_index++;
				return {arg.type_index.withCategory(arg.typeEnum()), &arg};
			}
			return {type_index, nullptr};
		}
		if (type_index.category() == TypeCategory::UserDefined) {
			const TypeInfo* ti = tryGetTypeInfo(type_index);
			if (!ti) {
				return {type_index, nullptr};
			}
			std::string_view tn = StringTable::getStringView(ti->name());

			// Check inner template params first
			for (size_t i = 0; i < template_params.size(); ++i) {
				const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
				if (tparam.name() == tn && i < template_args.size()) {
					return {template_args[i].type_index.withCategory(template_args[i].typeEnum()), &template_args[i]};
				}
			}
			// Check outer template params (e.g., T→int from class template)
			if (outer_binding) {
				for (size_t i = 0; i < outer_binding->param_names.size() && i < outer_binding->param_args.size(); ++i) {
					if (StringTable::getStringView(outer_binding->param_names[i]) == tn) {
						return {outer_binding->param_args[i].type_index.withCategory(outer_binding->param_args[i].typeEnum()), &outer_binding->param_args[i]};
					}
				}
			}
		}
		if (type_index.category() == TypeCategory::Struct ||
			type_index.category() == TypeCategory::UserDefined) {
			const TypeInfo* ti = tryGetTypeInfo(type_index);
			if (ti && ti->isTemplateInstantiation()) {
				std::vector<TemplateTypeArg> concrete_args =
					materializePlaceholderTemplateArgs(*ti, template_params, template_args);

				if (outer_binding) {
					for (auto& concrete_arg : concrete_args) {
						if (!concrete_arg.is_dependent || !concrete_arg.dependent_name.isValid()) {
							continue;
						}

						std::string_view dep_name =
							StringTable::getStringView(concrete_arg.dependent_name);
						for (size_t i = 0;
							 i < outer_binding->param_names.size() &&
							 i < outer_binding->param_args.size();
							 ++i) {
							if (StringTable::getStringView(outer_binding->param_names[i]) ==
								dep_name) {
								concrete_arg = outer_binding->param_args[i];
								break;
							}
						}
					}
				}

				const bool all_resolved =
					!concrete_args.empty() &&
					std::none_of(
						concrete_args.begin(),
						concrete_args.end(),
						[](const TemplateTypeArg& arg) { return arg.is_dependent; });
				if (all_resolved) {
					std::string_view base_template_name =
						StringTable::getStringView(ti->baseTemplateName());
					if (!base_template_name.empty()) {
						try_instantiate_class_template(base_template_name, concrete_args);
						std::string_view concrete_name =
							get_instantiated_class_name(base_template_name, concrete_args);
						auto concrete_it = getTypesByNameMap().find(
							StringTable::getOrInternStringHandle(concrete_name));
						if (concrete_it != getTypesByNameMap().end()) {
							return {
								concrete_it->second->type_index_.withCategory(TypeCategory::Struct),
								nullptr};
						}
					}
				}
			}
		}
		return {type_index, nullptr};
	};

	// Propagates function_signature to a substituted TypeSpecifierNode:
	// prefers the original type spec's signature, falls back to the template arg's signature.
	auto propagate_function_signature = [](TypeSpecifierNode& target,
										   const TypeSpecifierNode& original, const TemplateTypeArg* resolved_arg) {
		if (original.has_function_signature()) {
			target.set_function_signature(original.function_signature());
		} else if (resolved_arg && resolved_arg->function_signature.has_value()) {
			target.set_function_signature(*resolved_arg->function_signature);
		}
	};
	auto apply_resolved_type_metadata = [](TypeSpecifierNode& target, const TemplateTypeArg* resolved_arg, TypeIndex source_type_index) {
		if (resolved_arg) {
			for (size_t i = 0; i < resolved_arg->pointer_depth; ++i) {
				CVQualifier cv = i < resolved_arg->pointer_cv_qualifiers.size()
									 ? resolved_arg->pointer_cv_qualifiers[i]
									 : CVQualifier::None;
				target.add_pointer_level(cv);
			}
			if (target.reference_qualifier() == ReferenceQualifier::None &&
				resolved_arg->ref_qualifier != ReferenceQualifier::None) {
				target.set_reference_qualifier(resolved_arg->ref_qualifier);
			}
		}

		if (source_type_index.is_valid()) {
			const ResolvedAliasTypeInfo alias_info = resolveAliasTypeInfo(source_type_index);
			if (alias_info.type_index.is_valid() && alias_info.type_index != source_type_index) {
				target.set_type_index(alias_info.type_index.withCategory(alias_info.typeEnum()));
			}
			target.add_pointer_levels(static_cast<int>(alias_info.pointer_depth));
			if (target.reference_qualifier() == ReferenceQualifier::None &&
				alias_info.reference_qualifier != ReferenceQualifier::None) {
				target.set_reference_qualifier(alias_info.reference_qualifier);
			}
			if (!target.has_function_signature() && alias_info.function_signature.has_value()) {
				target.set_function_signature(*alias_info.function_signature);
			}
		}

		const int resolved_size_bits = getTypeSpecSizeBits(target);
		if (resolved_size_bits > 0) {
			target.set_size_in_bits(resolved_size_bits);
		}
	};

	// Substitute the return type if it's a template parameter
	const TypeSpecifierNode& return_type_spec = orig_decl.type_node().as<TypeSpecifierNode>();
	auto [return_type_index, return_resolved_arg] = resolve_template_type(return_type_spec.type_index());

	// Create mangled token
	Token mangled_token(Token::Type::Identifier, mangled_name,
						orig_decl.identifier_token().line(), orig_decl.identifier_token().column(),
						orig_decl.identifier_token().file_index());

	// Create return type node
	ASTNode substituted_return_type = emplace_node<TypeSpecifierNode>(
		return_type_index.category(),
		TypeQualifier::None,
		get_type_size_bits(return_type_index.category()),
		Token(),
		CVQualifier::None);

	// Copy pointer levels and set type_index from the resolved type
	auto& substituted_return_type_spec = substituted_return_type.as<TypeSpecifierNode>();
	substituted_return_type_spec.set_type_index(return_type_index);
	for (const auto& ptr_level : return_type_spec.pointer_levels()) {
		substituted_return_type_spec.add_pointer_level(ptr_level.cv_qualifier);
	}
	substituted_return_type_spec.set_reference_qualifier(return_type_spec.reference_qualifier());
	propagate_function_signature(substituted_return_type_spec, return_type_spec, return_resolved_arg);
	apply_resolved_type_metadata(substituted_return_type_spec, return_resolved_arg, return_type_index);

	// Create the new function declaration
	auto [new_func_decl_node, new_func_decl_ref] = emplace_node_ref<DeclarationNode>(substituted_return_type, mangled_token);
	auto [new_func_node, new_func_ref] = emplace_node_ref<FunctionDeclarationNode>(new_func_decl_ref, struct_name);

	std::unordered_map<TypeIndex, TemplateTypeArg> default_type_sub_map;
	std::unordered_map<std::string_view, int64_t> default_nontype_sub_map;
	std::unordered_map<std::string_view, TemplateTypeArg> default_param_map;
	std::vector<std::string_view> default_param_order;
	if (outer_binding) {
		for (size_t i = 0; i < outer_binding->param_names.size() && i < outer_binding->param_args.size(); ++i) {
			std::string_view param_name = StringTable::getStringView(outer_binding->param_names[i]);
			default_param_order.push_back(param_name);
			default_param_map[param_name] = outer_binding->param_args[i];
			auto type_it = getTypesByNameMap().find(outer_binding->param_names[i]);
			if (type_it != getTypesByNameMap().end()) {
				default_type_sub_map[type_it->second->type_index_] = outer_binding->param_args[i];
			} else {
				default_type_sub_map[TypeIndex{getTypeInfoCount() + default_type_sub_map.size() + 1}] = outer_binding->param_args[i];
			}
		}
	}
	for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
		if (!template_params[i].is<TemplateParameterNode>())
			continue;
		const auto& template_param = template_params[i].as<TemplateParameterNode>();
		default_param_order.push_back(template_param.name());
		default_param_map[template_param.name()] = template_args[i];
		if (template_param.kind() == TemplateParameterKind::Type && !template_args[i].is_value) {
			auto type_it = getTypesByNameMap().find(template_param.nameHandle());
			if (type_it != getTypesByNameMap().end()) {
				default_type_sub_map[type_it->second->type_index_] = template_args[i];
			} else {
				default_type_sub_map[TypeIndex{getTypeInfoCount() + default_type_sub_map.size() + 1}] = template_args[i];
			}
		} else if (template_param.kind() == TemplateParameterKind::NonType && template_args[i].is_value) {
			default_nontype_sub_map[template_param.name()] = template_args[i].value;
		}
	}

	// Save and reset pack_param_info_ so this instantiation can rebuild its local pack state.
	auto saved_pack_param_info = std::move(pack_param_info_);
	pack_param_info_.clear();

	// Helper to extract the type name from a TypeSpecifierNode, trying token value first, then TypeInfo lookup
	auto getTypeName = [&](const TypeSpecifierNode& type_spec) -> std::string_view {
		if (type_spec.category() != TypeCategory::UserDefined &&
			type_spec.category() != TypeCategory::TypeAlias &&
			type_spec.category() != TypeCategory::Template) {
			return {};
		}
		std::string_view name;
		if (type_spec.type_index().is_valid()) {
			if (const TypeInfo* ti = tryGetTypeInfo(type_spec.type_index())) {
				name = StringTable::getStringView(ti->name());
			}
		}
		if (name.empty()) {
			name = type_spec.token().value();
		}
		return name;
	};
	std::unordered_map<StringHandle, const TemplateParameterNode*, StringHash, StringEqual> tparam_nodes_by_name;
	for (const auto& template_param_node : template_params) {
		if (!template_param_node.is<TemplateParameterNode>()) {
			continue;
		}
		const auto& template_param = template_param_node.as<TemplateParameterNode>();
		tparam_nodes_by_name.emplace(template_param.nameHandle(), &template_param);
	}
	std::vector<size_t> template_param_arg_starts(template_params.size(), SIZE_MAX);
	std::vector<size_t> template_param_arg_counts(template_params.size(), 0);
	{
		size_t template_arg_index = 0;
		for (size_t i = 0; i < template_params.size(); ++i) {
			if (!template_params[i].is<TemplateParameterNode>()) {
				continue;
			}
			const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
			if (tparam.is_variadic()) {
				size_t pack_count = 0;
				if (deduction_info.has_value()) {
					const size_t function_pack_count =
						deduction_info->function_pack_call_arg_end > deduction_info->function_pack_call_arg_start
							? deduction_info->function_pack_call_arg_end - deduction_info->function_pack_call_arg_start
							: 0;
					const bool is_primary = tparam.nameHandle() ==
						deduction_info->function_pack_template_param_name;
					const bool is_co_pack =
						deduction_info->function_pack_dependent_param_names.count(tparam.nameHandle());
					if (is_primary || is_co_pack) {
						pack_count = function_pack_count;
					}
				}
				if (pack_count == 0) {
					size_t remaining_args = template_arg_index < template_args.size()
											 ? template_args.size() - template_arg_index
											 : 0;
					size_t required_after =
						countRequiredTemplateArgsAfter<InlineVector<ASTNode, 4>, InlineVector<TemplateTypeArg, 4>>(
							template_params, i + 1);
					pack_count = remaining_args > required_after
								 ? remaining_args - required_after
								 : 0;
				}
				template_param_arg_starts[i] = template_arg_index;
				template_param_arg_counts[i] = pack_count;
				template_arg_index += pack_count;
				continue;
			}
			if (template_arg_index < template_args.size()) {
				template_param_arg_starts[i] = template_arg_index;
				template_param_arg_counts[i] = 1;
				++template_arg_index;
			}
		}
	}
	auto getPackParameterName = [&](const TypeSpecifierNode& type_spec,
								   StringHandle& primary_pack_name,
								   std::unordered_set<StringHandle, StringHash, StringEqual>& dependent_pack_names) -> std::string_view {
		primary_pack_name = {};
		dependent_pack_names.clear();
		std::string_view type_name = getTypeName(type_spec);
		if (!type_name.empty()) {
			StringHandle type_name_handle = StringTable::getOrInternStringHandle(type_name);
			if (tparam_nodes_by_name.count(type_name_handle)) {
				primary_pack_name = type_name_handle;
				dependent_pack_names.insert(type_name_handle);
			}
		}
		collectDependentTemplateParamNamesFromType(
			type_spec.type_index(),
			tparam_nodes_by_name,
			primary_pack_name,
			dependent_pack_names);
		return primary_pack_name.isValid()
			? StringTable::getStringView(primary_pack_name)
			: type_name;
	};
	auto getTemplateParamPackBinding = [&](std::string_view pack_param_name) -> std::optional<std::pair<size_t, size_t>> {
		for (size_t i = 0; i < template_params.size(); ++i) {
			if (!template_params[i].is<TemplateParameterNode>()) {
				continue;
			}
			const auto& tparam = template_params[i].as<TemplateParameterNode>();
			if (tparam.is_variadic() && tparam.name() == pack_param_name) {
				return std::pair<size_t, size_t>{
					template_param_arg_starts[i],
					template_param_arg_counts[i]};
			}
		}
		return std::nullopt;
	};
	auto cloneNonVariadicTemplateParam = [&](const TemplateParameterNode& param) {
		TemplateParameterNode clone = [&]() {
			switch (param.kind()) {
				case TemplateParameterKind::Type:
					return TemplateParameterNode(param.nameHandle(), param.token());
				case TemplateParameterKind::NonType:
					return TemplateParameterNode(param.nameHandle(), param.type_node(), param.token());
				case TemplateParameterKind::Template:
					return TemplateParameterNode(param.nameHandle(), param.nested_parameters(), param.token());
			}
			return TemplateParameterNode(param.nameHandle(), param.token());
		}();
		if (param.has_concept_constraint()) {
			clone.set_concept_constraint(param.concept_constraint());
		}
		if (param.has_concept_args()) {
			clone.set_concept_args(param.concept_args());
		}
		if (param.has_default()) {
			clone.set_default_value(param.default_value());
		}
		if (param.has_default_value_position()) {
			clone.set_default_value_position(param.default_value_position());
		}
		clone.set_registered_type_index(param.registered_type_index());
		return clone;
	};
	auto buildSubstitutionForPackElement =
		[&](std::string_view pack_param_name, size_t pack_element_offset,
			const std::unordered_set<StringHandle, StringHash, StringEqual>& dependent_pack_names,
			InlineVector<ASTNode, 4>& subst_params,
			InlineVector<TemplateTypeArg, 4>& subst_args) -> bool {
			auto pack_binding = getTemplateParamPackBinding(pack_param_name);
			if (!pack_binding.has_value() || pack_element_offset >= pack_binding->second) {
				return false;
			}
			for (size_t i = 0; i < template_params.size(); ++i) {
				if (!template_params[i].is<TemplateParameterNode>()) {
					continue;
				}
				const auto& tparam = template_params[i].as<TemplateParameterNode>();
				if (tparam.is_variadic()) {
					const bool is_primary = (tparam.name() == pack_param_name);
					const bool is_co_pack = !is_primary &&
						dependent_pack_names.count(tparam.nameHandle());
					if (is_primary || is_co_pack) {
						size_t pack_index = template_param_arg_starts[i] + pack_element_offset;
						if (pack_index < template_args.size()) {
							subst_params.push_back(emplace_node<TemplateParameterNode>(
								cloneNonVariadicTemplateParam(tparam)));
							subst_args.push_back(template_args[pack_index]);
						}
					}
					continue;
				}
				if (template_param_arg_starts[i] == SIZE_MAX || template_param_arg_starts[i] >= template_args.size()) {
					continue;
				}
				subst_params.push_back(template_params[i]);
				subst_args.push_back(template_args[template_param_arg_starts[i]]);
			}
			return true;
		};
	auto buildMaterializedParamType =
		[&](const TypeSpecifierNode& original_param_type,
			const InlineVector<ASTNode, 4>& materialized_template_params,
			const InlineVector<TemplateTypeArg, 4>& materialized_template_args) {
			TypeIndex substituted_type_index = substitute_template_parameter(
				original_param_type, materialized_template_params, materialized_template_args);
			ASTNode param_type = emplace_node<TypeSpecifierNode>(
				substituted_type_index,
				get_type_size_bits(substituted_type_index.category()),
				original_param_type.token(),
				original_param_type.cv_qualifier(),
				ReferenceQualifier::None);
			TypeSpecifierNode& param_type_ref = param_type.as<TypeSpecifierNode>();
			param_type_ref.set_reference_qualifier(original_param_type.reference_qualifier());
			applyTemplateArgIndirection(
				param_type_ref,
				original_param_type,
				materialized_template_params,
				materialized_template_args,
				/*propagate_reference_qualifier=*/true);
			for (const auto& ptr_level : original_param_type.pointer_levels()) {
				param_type_ref.add_pointer_level(ptr_level.cv_qualifier);
			}
			propagateFunctionSignatureFromTemplateArg(
				param_type_ref,
				original_param_type,
				substituted_type_index,
				materialized_template_params,
				materialized_template_args);
			normalizeSubstitutedTypeSpec(param_type_ref);
			return param_type;
		};

	// Copy parameters while substituting template arguments and expanding variadic packs.
	for (const auto& param : func_decl.parameter_nodes()) {
		if (param.is<DeclarationNode>()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			const TypeSpecifierNode& param_type_spec = param_decl.type_node().as<TypeSpecifierNode>();

			// Expand variadic pack parameters (including wrapped nested pack element types).
			bool handled_as_pack = false;
			bool is_pack_param = param_decl.is_parameter_pack();

			// Also detect if type references a variadic template parameter (for cases where is_parameter_pack isn't set)
			std::string_view type_name = getTypeName(param_type_spec);
			if (!is_pack_param && !type_name.empty()) {
				for (size_t i = 0; i < template_params.size(); ++i) {
					if (!template_params[i].is<TemplateParameterNode>())
						continue;
					const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
					if (tparam.is_variadic() && tparam.name() == type_name) {
						is_pack_param = true;
						break;
					}
				}
				if (!is_pack_param) {
					StringHandle nested_primary_name;
					std::unordered_set<StringHandle, StringHash, StringEqual> nested_dependent_names;
					collectDependentTemplateParamNamesFromType(
						param_type_spec.type_index(),
						tparam_nodes_by_name,
						nested_primary_name,
						nested_dependent_names);
					for (StringHandle dep_name : nested_dependent_names) {
						auto dep_it = tparam_nodes_by_name.find(dep_name);
						if (dep_it != tparam_nodes_by_name.end() && dep_it->second->is_variadic()) {
							is_pack_param = true;
							break;
						}
					}
				}
			}

			if (is_pack_param) {
				StringHandle primary_pack_name;
				std::unordered_set<StringHandle, StringHash, StringEqual> dependent_pack_names;
				std::string_view pack_param_name = getPackParameterName(
					param_type_spec,
					primary_pack_name,
					dependent_pack_names);
				auto pack_binding = getTemplateParamPackBinding(pack_param_name);
				if (pack_binding.has_value()) {
					std::string_view orig_name = param_decl.identifier_token().value();
					for (size_t pi = 0; pi < pack_binding->second; ++pi) {
						InlineVector<ASTNode, 4> subst_params;
						InlineVector<TemplateTypeArg, 4> subst_args;
						if (!buildSubstitutionForPackElement(
								pack_param_name,
								pi,
								dependent_pack_names,
								subst_params,
								subst_args)) {
							continue;
						}

						ASTNode param_type = buildMaterializedParamType(
							param_type_spec,
							subst_params,
							subst_args);

						StringBuilder name_builder;
						name_builder.append(orig_name).append('_').append(pi);
						Token elem_token(Token::Type::Identifier, name_builder.commit(),
										 param_decl.identifier_token().line(),
										 param_decl.identifier_token().column(),
										 param_decl.identifier_token().file_index());
						new_func_ref.add_parameter_node(emplace_node<DeclarationNode>(param_type, elem_token));
					}
					pack_param_info_.push_back({orig_name, 0, pack_binding->second});
					handled_as_pack = true;
				}
			}
			if (handled_as_pack)
				continue;

			// Resolve the template parameter type (to get function_signature if available)
			auto [param_type_index, resolved_arg] = resolve_template_type(param_type_spec.type_index());

			// Create the substituted parameter type specifier
			auto substituted_param_type = emplace_node<TypeSpecifierNode>(
				param_type_index.category(),
				TypeQualifier::None,
				get_type_size_bits(param_type_index.category()),
				Token(), CVQualifier::None);

			// Copy pointer levels and set type_index from the resolved type
			auto& substituted_param_type_spec = substituted_param_type.as<TypeSpecifierNode>();
			substituted_param_type_spec.set_type_index(param_type_index);
			for (const auto& ptr_level : param_type_spec.pointer_levels()) {
				substituted_param_type_spec.add_pointer_level(ptr_level.cv_qualifier);
			}
			substituted_param_type_spec.set_reference_qualifier(param_type_spec.reference_qualifier());
			propagate_function_signature(substituted_param_type_spec, param_type_spec, resolved_arg);
			apply_resolved_type_metadata(substituted_param_type_spec, resolved_arg, param_type_index);

			// Create the new parameter declaration
			auto new_param_decl = emplace_node<DeclarationNode>(substituted_param_type, param_decl.identifier_token());
			if (param_decl.has_default_value()) {
				ExpressionSubstitutor substitutor(default_param_map, *this, default_param_order);
				ASTNode substituted_default = substitutor.substitute(param_decl.default_value());
				if (substituted_default.is<ExpressionNode>() &&
					std::holds_alternative<ConstructorCallNode>(substituted_default.as<ExpressionNode>())) {
					substituted_default = substitute_template_params_in_expression(
						substituted_default, default_type_sub_map, default_nontype_sub_map, StringHandle{});
				}
				new_param_decl.as<DeclarationNode>().set_default_value(substituted_default);
			}
			new_func_ref.add_parameter_node(new_param_decl);
		}
	}

	copy_function_properties(new_func_ref, func_decl);
	auto orig_body = func_decl.get_definition();

	// Check if the template has a body position stored
	if (!func_decl.has_template_body_position()) {
		if (orig_body.has_value()) {
			new_func_ref.set_definition(
				substituteTemplateParameters(*orig_body, template_params, template_args));
			finalize_function_after_definition(new_func_ref);
		} else {
			compute_and_set_mangled_name(new_func_ref);
		}
		registerAndNormalizeLateMaterializedTopLevelNode(new_func_node);
		gTemplateRegistry.registerInstantiation(key, new_func_node);
		return new_func_node;
	}

	// Temporarily add the concrete types to the type system with template parameter names
	FlashCpp::TemplateParameterScope template_scope;
	InlineVector<StringHandle, 4> param_names;
	for (const auto& tparam_node : template_params) {
		if (tparam_node.is<TemplateParameterNode>()) {
			param_names.push_back(tparam_node.as<TemplateParameterNode>().nameHandle());
		}
	}

	// Kind::Value and Kind::Template entries are intentionally skipped by registerTypeParamsInScope:
	// registering them would poison getTypesByNameMap() with Invalid/garbage TypeInfo entries.
	registerTypeParamsInScope(template_params, template_args, template_scope, false);

	// Also add outer template parameter bindings (e.g., T→int from class template)
	if (outer_binding) {
		registerOuterBindingInScope(*outer_binding, template_scope);
		FLASH_LOG(Templates, Debug, "Added ", outer_binding->param_names.size(), " outer template param bindings for body parsing");
	}

	// Save current position
	SaveHandle current_pos = save_token_position();

	// Restore to the function body start (lexer only - keep AST nodes from previous instantiations)
	restore_lexer_position_only(func_decl.template_body_position());

	// Look up the struct type info
	auto struct_type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(struct_name));
	if (struct_type_it == getTypesByNameMap().end()) {
		FLASH_LOG(Templates, Debug, "Struct type not found: ", struct_name);
		restore_token_position(current_pos);
		return std::nullopt;
	}

	const TypeInfo* struct_type_info = struct_type_it->second;
	TypeIndex struct_type_index = struct_type_info->type_index_;

	// Set up parsing context for the member function
	gSymbolTable.enter_scope(ScopeType::Function);
	current_function_ = &new_func_ref;

	// Find the struct node
	StructDeclarationNode* struct_node_ptr = nullptr;
	for (auto& node : ast_nodes_) {
		if (node.is<StructDeclarationNode>()) {
			auto& sn = node.as<StructDeclarationNode>();
			if (sn.name() == struct_name) {
				struct_node_ptr = &sn;
				break;
			}
		}
	}

	member_function_context_stack_.push_back({
		StringTable::getOrInternStringHandle(struct_name),
		struct_type_index,
		struct_node_ptr,
		nullptr	// local_struct_info - not needed for out-of-class member function definitions
	});

	// Add 'this' pointer to symbol table
	ASTNode this_type = emplace_node<TypeSpecifierNode>(
		struct_type_index.withCategory(TypeCategory::Struct),
		64,	// Pointer size
		Token(),
		CVQualifier::None,
		ReferenceQualifier::None);

	Token this_token(Token::Type::Keyword, "this"sv, 0, 0, 0);
	auto this_decl = emplace_node<DeclarationNode>(this_type, this_token);
	gSymbolTable.insert("this"sv, this_decl);

	// Add parameters to symbol table
	for (const auto& param : new_func_ref.parameter_nodes()) {
		if (param.is<DeclarationNode>()) {
			const auto& param_decl = param.as<DeclarationNode>();
			gSymbolTable.insert(param_decl.identifier_token().value(), param);
		}
	}

	// Push class template pack info so sizeof...() from the enclosing class template
	// can be resolved during member function template body parsing.
	// E.g., sizeof...(_Elements) inside a member function template of tuple<int, float>.
	ClassTemplatePackGuard member_pack_guard(class_template_pack_stack_);
	{
		auto pack_it = class_template_pack_registry_.find(StringTable::getOrInternStringHandle(struct_name));
		if (pack_it != class_template_pack_registry_.end()) {
			member_pack_guard.push(pack_it->second);
		}
	}

	// Set up template parameter substitutions for body parsing so that non-type
	// parameters (e.g., N in "return N;") are resolved during parse_function_body().
	// This mirrors the setup performed by try_instantiate_single_template and
	// try_instantiate_template_explicit for free function templates.
	{
		FlashCpp::ScopedState guard_subs(template_param_substitutions_);
		populateTemplateParamSubstitutions(template_param_substitutions_, template_params, template_args);

		// Parse the function body
		{
			FlashCpp::ScopedState guard_param_names(currentTemplateParamState());
			for (const auto& pn : param_names) {
				pushCurrentTemplateParamName(pn);
			}

			auto block_result = parse_function_body();  // handles function-try-blocks
			if (!block_result.is_error() && block_result.node().has_value()) {
				// Substitute template parameters in the body (handles sizeof..., fold expressions, etc.)
				ASTNode substituted_body = substituteTemplateParameters(
					*block_result.node(),
					template_params,
					template_args);
				if (outer_binding && !outer_binding->param_names.empty()) {
					InlineVector<ASTNode, 4> outer_params;
					InlineVector<TemplateTypeArg, 4> outer_args;
					for (size_t i = 0;
						 i < outer_binding->param_names.size() &&
						 i < outer_binding->param_args.size();
						 ++i) {
						Token outer_param_token(
							Token::Type::Identifier,
							StringTable::getStringView(outer_binding->param_names[i]),
							0, 0, 0);
						outer_params.push_back(emplace_node<TemplateParameterNode>(
							outer_binding->param_names[i],
							outer_param_token));
						outer_args.push_back(outer_binding->param_args[i]);
					}
					substituted_body = substituteTemplateParameters(
						substituted_body,
						outer_params,
						outer_args);
				}
				new_func_ref.set_definition(substituted_body);
			}
		} // current_template_param_names_ restored here
	} // template_param_substitutions_ restored here

	// Clean up context
	current_function_ = nullptr;
	member_function_context_stack_.pop_back();
	gSymbolTable.exit_scope();

	// Restore original position (lexer only - keep AST nodes we created)
	restore_lexer_position_only(current_pos);

	// template_scope RAII guard automatically removes temporary type infos

	// Add the instantiated function to the AST
	registerAndNormalizeLateMaterializedTopLevelNode(new_func_node);

	// Update the saved position to include this new node so it doesn't get erased
	saved_tokens_[current_pos].ast_nodes_size_ = ast_nodes_.size();

	if (new_func_ref.is_materialized()) {
		finalize_function_after_definition(new_func_ref);
	} else {
		compute_and_set_mangled_name(new_func_ref);
	}

	// Register the instantiation
	gTemplateRegistry.registerInstantiation(key, new_func_node);

	// Restore the outer scope's pack_param_info_ after completing this instantiation.
	pack_param_info_ = std::move(saved_pack_param_info);

	return new_func_node;
}

// Instantiate a lazy member function on-demand
// This performs the template parameter substitution that was deferred during lazy registration
