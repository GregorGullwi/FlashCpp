#pragma once

ASTNode rebindStaticMemberInitializerFunctionCalls(
	const ASTNode& node,
	const StructTypeInfo* struct_info,
	bool set_qualified_name);

inline void normalizeSubstitutedTypeSpec(TypeSpecifierNode& type_spec) {
	const ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(type_spec.type_index());
	if (resolved_alias.type_index.is_valid()) {
		type_spec.set_type_index(resolved_alias.type_index.withCategory(resolved_alias.typeEnum()));
		type_spec.set_category(resolved_alias.typeEnum());
	}
	type_spec.add_pointer_levels(static_cast<int>(resolved_alias.pointer_depth));
	if (type_spec.reference_qualifier() == ReferenceQualifier::None &&
		resolved_alias.reference_qualifier != ReferenceQualifier::None) {
		type_spec.set_reference_qualifier(resolved_alias.reference_qualifier);
	}
	if (!type_spec.has_function_signature() && resolved_alias.function_signature.has_value()) {
		type_spec.set_function_signature(*resolved_alias.function_signature);
	}
	if (!resolved_alias.array_dimensions.empty()) {
		const std::span<const size_t> type_dimensions = type_spec.array_dimensions();
		std::vector<size_t> array_dimensions(type_dimensions.begin(), type_dimensions.end());
		array_dimensions.insert(array_dimensions.end(),
								resolved_alias.array_dimensions.begin(),
								resolved_alias.array_dimensions.end());
		type_spec.set_array_dimensions(array_dimensions);
	}
	if (const int resolved_size_bits = getTypeSpecSizeBits(type_spec); resolved_size_bits > 0) {
		type_spec.set_size_in_bits(resolved_size_bits);
	}
}

inline int getSubstitutedTypeSizeBits(TypeIndex substituted_type_index) {
	const ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(substituted_type_index);
	TypeIndex size_type_index = resolved_alias.type_index.is_valid()
		? resolved_alias.type_index.withCategory(resolved_alias.typeEnum())
		: substituted_type_index;
	if (resolved_alias.pointer_depth > 0 ||
		resolved_alias.reference_qualifier != ReferenceQualifier::None ||
		size_type_index.category() == TypeCategory::FunctionPointer ||
		size_type_index.category() == TypeCategory::MemberFunctionPointer ||
		size_type_index.category() == TypeCategory::MemberObjectPointer) {
		return 64;
	}

	if (size_type_index.is_valid()) {
		if (const StructTypeInfo* struct_info = tryGetStructTypeInfo(size_type_index)) {
			return static_cast<int>(toSizeT(struct_info->sizeInBytes()) * 8);
		}
		if (const TypeInfo* type_info = tryGetTypeInfo(size_type_index)) {
			if (type_info->hasStoredSize()) {
				return static_cast<int>(toSizeT(type_info->sizeInBytes()) * 8);
			}
		}
	}

	return get_type_size_bits(size_type_index.category());
}

template <typename ParamContainer, typename ArgContainer>
inline std::optional<FunctionSignature> resolveTemplateFunctionPointerSignature(
	const TypeSpecifierNode& type_spec,
	TypeIndex substituted_type_index,
	const ParamContainer& template_params,
	const ArgContainer& template_args) {
	if (substituted_type_index.category() != TypeCategory::FunctionPointer &&
		substituted_type_index.category() != TypeCategory::MemberFunctionPointer)
		return std::nullopt;
	if (type_spec.has_function_signature())
		return type_spec.function_signature();

	StringHandle type_name_handle;
	if (const TypeInfo* ts_ti = tryGetTypeInfo(type_spec.type_index()))
		type_name_handle = ts_ti->name();
	if (!type_name_handle.isValid())
		type_name_handle = type_spec.token().handle();
	if (!type_name_handle.isValid())
		return std::nullopt;

	if (const auto* arg = findTemplateArgByName(type_name_handle.view(), template_params, template_args)) {
		if (arg->function_signature.has_value())
			return arg->function_signature;
	}
	return std::nullopt;
}

template <typename ParamContainer, typename ArgContainer>
inline void applyBoundTemplateArgMetadata(
	TypeSpecifierNode& substituted_type,
	const TypeSpecifierNode& original_type,
	const ParamContainer& template_params,
	const ArgContainer& template_args) {

	StringHandle type_name_handle;
	if (const TypeInfo* ts_ti = tryGetTypeInfo(original_type.type_index()))
		type_name_handle = ts_ti->name();
	if (!type_name_handle.isValid())
		type_name_handle = original_type.token().handle();
	if (!type_name_handle.isValid())
		return;

	const auto* arg = findTemplateArgByName(type_name_handle.view(), template_params, template_args);
	if (arg == nullptr || arg->is_value) {
		return;
	}

	for (size_t i = 0; i < arg->pointer_depth; ++i) {
		CVQualifier cv = i < arg->pointer_cv_qualifiers.size()
			? arg->pointer_cv_qualifiers[i]
			: CVQualifier::None;
		substituted_type.add_pointer_level(cv);
	}
	substituted_type.set_reference_qualifier(collapseReferenceQualifiers(
		arg->ref_qualifier,
		substituted_type.reference_qualifier()));
	if (!substituted_type.has_function_signature() && arg->function_signature.has_value()) {
		substituted_type.set_function_signature(*arg->function_signature);
	}
	const int resolved_size_bits = getTypeSpecSizeBits(substituted_type);
	if (resolved_size_bits > 0) {
		substituted_type.set_size_in_bits(resolved_size_bits);
	}
}

template <typename TSubstituteFn, typename TOwnerDecl, typename TParams, typename TArgs>
TypeIndex resolveOwnerAliasTypeIndex(
	TSubstituteFn&& substitute_fn,
	const TOwnerDecl& owner_decl,
	const TypeSpecifierNode& original_type_spec,
	const TParams& tmpl_params,
	const TArgs& tmpl_args,
	TypeIndex current_type_index) {
	if (current_type_index.is_valid()) {
		return current_type_index.withCategory(current_type_index.category());
	}
	if (original_type_spec.type() != TypeCategory::UserDefined &&
		original_type_spec.type() != TypeCategory::TypeAlias &&
		original_type_spec.type() != TypeCategory::Template) {
		return current_type_index;
	}
	StringHandle type_name_handle = original_type_spec.token().handle();
	if (!type_name_handle.isValid()) {
		return current_type_index;
	}
	for (const auto& type_alias : owner_decl.type_aliases()) {
		if (type_alias.alias_name != type_name_handle ||
			!type_alias.type_node.template is<TypeSpecifierNode>()) {
			continue;
		}
		TypeIndex substituted_alias = substitute_fn(
			type_alias.type_node.template as<TypeSpecifierNode>(),
			tmpl_params,
			tmpl_args);
		if (substituted_alias.category() != TypeCategory::UserDefined || substituted_alias.is_valid()) {
			return substituted_alias.withCategory(substituted_alias.category());
		}
		break;
	}
	return current_type_index;
}

inline TypeIndex resolveDependentMemberPlaceholderFromOwnerArtifact(
	const TypeSpecifierNode& original_type_spec,
	TypeIndex substituted_type_index) {
	if (!substituted_type_index.is_valid()) {
		return substituted_type_index;
	}
	const TypeInfo* owner_type_info = tryGetTypeInfo(substituted_type_index);
	if (owner_type_info == nullptr ||
		!is_struct_type(owner_type_info->typeEnum())) {
		return substituted_type_index;
	}

	const TypeInfo* original_type_info = nullptr;
	if (original_type_spec.type_index().is_valid()) {
		original_type_info = tryGetTypeInfo(original_type_spec.type_index());
	}
	const TypeInfo::DependentQualifiedNameRecord* dependent_record =
		original_type_info != nullptr && original_type_info->hasDependentQualifiedName()
			? original_type_info->dependentQualifiedName()
			: nullptr;

	StringBuilder qualified_member_name_builder;
	qualified_member_name_builder.append(
		StringTable::getStringView(owner_type_info->name()));
	bool appended_member = false;
	if (dependent_record != nullptr && !dependent_record->member_chain.empty()) {
		for (const auto& member : dependent_record->member_chain) {
			if (!member.name.isValid() || member.has_template_arguments) {
				return substituted_type_index;
			}
			qualified_member_name_builder.append("::");
			qualified_member_name_builder.append(StringTable::getStringView(member.name));
			appended_member = true;
		}
	} else {
		std::string_view member_suffix;
		if (original_type_info != nullptr && original_type_info->name().isValid()) {
			std::string_view original_type_name =
				StringTable::getStringView(original_type_info->name());
			if (size_t scope_pos = original_type_name.find("::");
				scope_pos != std::string_view::npos) {
				member_suffix = original_type_name.substr(scope_pos + 2);
			}
		}
		const std::string_view token_name = original_type_spec.token().value();
		if (member_suffix.empty()) {
			member_suffix = token_name;
		}
		if (member_suffix.empty()) {
			return substituted_type_index;
		}
		if (member_suffix.find("::") == std::string_view::npos) {
			qualified_member_name_builder.append("::");
			qualified_member_name_builder.append(member_suffix);
			appended_member = true;
		} else {
			size_t start = 0;
			while (start < member_suffix.size()) {
				size_t sep = member_suffix.find("::", start);
				std::string_view component = sep == std::string_view::npos
					? member_suffix.substr(start)
					: member_suffix.substr(start, sep - start);
				if (!component.empty()) {
					qualified_member_name_builder.append("::");
					qualified_member_name_builder.append(component);
					appended_member = true;
				}
				if (sep == std::string_view::npos) {
					break;
				}
				start = sep + 2;
			}
		}
	}

	if (!appended_member) {
		return substituted_type_index;
	}

	const std::string_view qualified_member_name = qualified_member_name_builder.commit();
	if (qualified_member_name.empty()) {
		return substituted_type_index;
	}

	const TypeInfo* resolved_member_type_info = findTypeByName(
		StringTable::getOrInternStringHandle(qualified_member_name));
	if (resolved_member_type_info == nullptr) {
		return substituted_type_index;
	}

	const TypeIndex resolved_type_index = resolved_member_type_info->registeredTypeIndex().withCategory(
		resolved_member_type_info->typeEnum());
	if (!resolved_type_index.is_valid()) {
		return substituted_type_index;
	}

	const ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(resolved_type_index);
	if (resolved_alias.type_index.is_valid()) {
		return resolved_alias.type_index.withCategory(resolved_alias.typeEnum());
	}
	return resolved_type_index;
}

inline TypeIndex resolveDependentMemberPlaceholderFromOwnerArtifact(
	const ASTNode& original_type_node,
	const TypeSpecifierNode& original_type_spec,
	TypeIndex substituted_type_index) {
	TypeIndex resolved_type_index = resolveDependentMemberPlaceholderFromOwnerArtifact(
		original_type_spec,
		substituted_type_index);
	if (!resolved_type_index.is_valid()) {
		return resolved_type_index;
	}

	const TypeInfo* owner_type_info = tryGetTypeInfo(resolved_type_index);
	if (owner_type_info == nullptr ||
		!is_struct_type(owner_type_info->typeEnum())) {
		return resolved_type_index;
	}

	StringHandle member_name{};
	if (original_type_node.is<QualifiedIdentifierNode>()) {
		member_name = original_type_node.as<QualifiedIdentifierNode>().nameHandle();
	} else if (original_type_node.is<ExpressionNode>()) {
		const ExpressionNode& expr = original_type_node.as<ExpressionNode>();
		if (const auto* qualified = std::get_if<QualifiedIdentifierNode>(&expr)) {
			member_name = qualified->nameHandle();
		}
	}
	if (!member_name.isValid()) {
		member_name = original_type_spec.token().handle();
	}
	if (!member_name.isValid() || member_name == owner_type_info->name()) {
		return resolved_type_index;
	}

	StringBuilder qualified_member_name_builder;
	qualified_member_name_builder
		.append(StringTable::getStringView(owner_type_info->name()))
		.append("::")
		.append(StringTable::getStringView(member_name));
	const std::string_view qualified_member_name = qualified_member_name_builder.commit();
	const TypeInfo* resolved_member_type_info = findTypeByName(
		StringTable::getOrInternStringHandle(qualified_member_name));
	if (resolved_member_type_info == nullptr) {
		return resolved_type_index;
	}

	const TypeIndex resolved_member_type_index =
		resolved_member_type_info->registeredTypeIndex().withCategory(
			resolved_member_type_info->typeEnum());
	const ResolvedAliasTypeInfo resolved_alias =
		resolveAliasTypeInfo(resolved_member_type_index);
	return resolved_alias.type_index.is_valid()
		? resolved_alias.type_index.withCategory(resolved_alias.typeEnum())
		: resolved_member_type_index;
}

template <typename NameContainer, typename ArgInfoContainer>
inline std::optional<TypeIndex> resolveStampedOuterTemplateBindingType(
	const NameContainer& outer_param_names,
	const ArgInfoContainer& outer_arg_infos,
	StringHandle dependent_owner_name) {
	if (!dependent_owner_name.isValid()) {
		return std::nullopt;
	}
	for (size_t i = 0;
		 i < outer_param_names.size() && i < outer_arg_infos.size();
		 ++i) {
		if (outer_param_names[i] != dependent_owner_name ||
			outer_arg_infos[i].is_value) {
			continue;
		}
		return outer_arg_infos[i].type_index.withCategory(
			outer_arg_infos[i].typeEnum());
	}
	return std::nullopt;
}

template <typename NameContainer, typename ArgInfoContainer>
inline std::optional<TypeIndex> resolveDependentPlaceholderFromOuterBindings(
	const TypeInfo* type_info,
	const NameContainer& outer_param_names,
	const ArgInfoContainer& outer_arg_infos) {
	if (type_info == nullptr || !type_info->isDependentPlaceholder()) {
		return std::nullopt;
	}
	const auto* dependent_record = type_info->dependentQualifiedName();
	if (dependent_record == nullptr || !dependent_record->owner_name.isValid()) {
		return std::nullopt;
	}
	return resolveStampedOuterTemplateBindingType(
		outer_param_names,
		outer_arg_infos,
		dependent_record->owner_name);
}

template <typename ParamContainer, typename ArgContainer>
inline std::optional<TypeIndex> resolveDependentPlaceholderFromTemplateParams(
	const TypeInfo* type_info,
	const ParamContainer& tmpl_params,
	const ArgContainer& tmpl_args) {
	if (type_info == nullptr) {
		return std::nullopt;
	}
	const TypeInfo* dependent_type_info = type_info;
	if (!dependent_type_info->isDependentPlaceholder()) {
		const ResolvedAliasTypeInfo resolved_alias =
			resolveAliasTypeInfo(type_info->registeredTypeIndex().withCategory(type_info->typeEnum()));
		if (resolved_alias.terminal_type_info != nullptr &&
			resolved_alias.terminal_type_info->isDependentPlaceholder()) {
			dependent_type_info = resolved_alias.terminal_type_info;
		}
	}
	if (!dependent_type_info->isDependentPlaceholder()) {
		return std::nullopt;
	}
	const auto* dependent_record = dependent_type_info->dependentQualifiedName();
	if (dependent_record == nullptr || !dependent_record->owner_name.isValid()) {
		return std::nullopt;
	}
	for (size_t i = 0; i < tmpl_params.size() && i < tmpl_args.size(); ++i) {
		const TemplateParameterNode* template_param = tryGetTemplateParameterNode(tmpl_params[i]);
		if (template_param == nullptr ||
			template_param->nameHandle() != dependent_record->owner_name ||
			tmpl_args[i].is_value) {
			continue;
		}
		return tmpl_args[i].type_index.withCategory(tmpl_args[i].typeEnum());
	}
	return std::nullopt;
}

template <typename ParamContainer, typename ArgContainer, typename InstantiateFn>
inline TypeIndex resolveDependentMemberTemplatePlaceholderFromConcreteOwnerArtifact(
	const ASTNode* original_type_node,
	const TypeSpecifierNode& original_type_spec,
	const ParamContainer& template_params,
	const ArgContainer& template_args,
	InstantiateFn&& instantiate_class_template,
	TypeIndex substituted_type_index) {
	if (!substituted_type_index.is_valid()) {
		return substituted_type_index;
	}

	const TypeInfo* original_type_info = nullptr;
	if (original_type_spec.type_index().is_valid()) {
		original_type_info = tryGetTypeInfo(original_type_spec.type_index());
	}
	const TypeInfo::DependentQualifiedNameRecord* dependent_record =
		original_type_info != nullptr && original_type_info->isDependentPlaceholder()
			? original_type_info->dependentQualifiedName()
			: nullptr;
	if (dependent_record == nullptr && original_type_info != nullptr) {
		const ResolvedAliasTypeInfo resolved_alias =
			resolveAliasTypeInfo(original_type_info->registeredTypeIndex().withCategory(original_type_info->typeEnum()));
		if (resolved_alias.terminal_type_info != nullptr &&
			resolved_alias.terminal_type_info->hasDependentQualifiedName()) {
			dependent_record = resolved_alias.terminal_type_info->dependentQualifiedName();
		}
	}
	if (dependent_record == nullptr) {
		if (const TypeInfo* substituted_type_info = tryGetTypeInfo(substituted_type_index);
			substituted_type_info != nullptr && substituted_type_info->hasDependentQualifiedName()) {
			dependent_record = substituted_type_info->dependentQualifiedName();
		}
	}
	if (dependent_record == nullptr && original_type_node != nullptr) {
		if (original_type_node->is<QualifiedIdentifierNode>()) {
			dependent_record =
				original_type_node->as<QualifiedIdentifierNode>().dependentQualifiedName();
		} else if (original_type_node->is<ExpressionNode>()) {
			const ExpressionNode& expr = original_type_node->as<ExpressionNode>();
			if (const auto* qualified = std::get_if<QualifiedIdentifierNode>(&expr)) {
				dependent_record = qualified->dependentQualifiedName();
			}
		}
	}
	if (original_type_info != nullptr) {
		std::string_view base_template_name =
			original_type_info->isTemplateInstantiation()
				? StringTable::getStringView(original_type_info->baseTemplateName())
				: std::string_view{};
		std::string_view owner_name;
		std::string_view member_template_name;
		const size_t owner_sep = base_template_name.rfind("::");
		if (owner_sep != std::string_view::npos) {
			owner_name = base_template_name.substr(0, owner_sep);
			member_template_name = base_template_name.substr(owner_sep + 2);
		} else {
			std::string_view original_type_name =
				StringTable::getStringView(original_type_info->name());
			const size_t hash_pos = original_type_name.find('$');
			if (hash_pos != std::string_view::npos) {
				member_template_name = original_type_name.substr(0, hash_pos);
			}
		}
		if (owner_name.empty() &&
			dependent_record != nullptr &&
			dependent_record->owner_name.isValid()) {
			owner_name = StringTable::getStringView(dependent_record->owner_name);
		}
		if (!member_template_name.empty()) {
			auto try_resolve_for_owner = [&](const TypeInfo* owner_type_info) -> TypeIndex {
			if (owner_type_info != nullptr && is_struct_type(owner_type_info->typeEnum())) {
				std::vector<TemplateTypeArg> concrete_template_args;
				concrete_template_args.reserve(original_type_info->templateArgs().size());
				for (const auto& arg_info : original_type_info->templateArgs()) {
					TemplateTypeArg concrete_arg =
						materializeTemplateArg(arg_info, template_params, template_args);
					if (concrete_arg.is_dependent ||
						concrete_arg.dependent_name.isValid() ||
						concrete_arg.dependent_expr.has_value()) {
						return substituted_type_index;
					}
					concrete_template_args.push_back(std::move(concrete_arg));
				}
				const std::string concrete_template_name = std::string(StringBuilder()
					.append(StringTable::getStringView(owner_type_info->name()))
					.append("::")
					.append(member_template_name)
					.commit());
				std::optional<ASTNode> instantiated_member_template =
					instantiate_class_template(
						concrete_template_name,
						std::span<const TemplateTypeArg>(
							concrete_template_args.data(),
							concrete_template_args.size()),
						false);
				if (instantiated_member_template.has_value() &&
					instantiated_member_template->is<StructDeclarationNode>()) {
					std::string_view instantiated_name =
						StringTable::getStringView(
							instantiated_member_template->as<StructDeclarationNode>().name());
					std::string qualified_instantiated_name;
					if (instantiated_name.find("::") == std::string_view::npos) {
						qualified_instantiated_name = std::string(StringBuilder()
							.append(StringTable::getStringView(owner_type_info->name()))
							.append("::")
							.append(instantiated_name)
							.commit());
					}
					std::string_view original_type_name =
						StringTable::getStringView(original_type_info->name());
					const size_t member_sep = original_type_name.rfind("::");
					const std::string_view terminal_member =
						member_sep != std::string_view::npos
							? original_type_name.substr(member_sep + 2)
							: std::string_view{};
					if (!terminal_member.empty()) {
						auto lookup_member_type = [&](std::string_view instantiated_owner_name) -> const TypeInfo* {
							if (instantiated_owner_name.empty()) {
								return nullptr;
							}
							return findTypeByName(StringTable::getOrInternStringHandle(
								StringBuilder()
									.append(instantiated_owner_name)
									.append("::")
									.append(terminal_member)
									.commit()));
						};
						const TypeInfo* member_type_info =
							lookup_member_type(qualified_instantiated_name);
						if (member_type_info == nullptr) {
							member_type_info = lookup_member_type(instantiated_name);
						}
						if (member_type_info != nullptr) {
							return member_type_info->registeredTypeIndex().withCategory(
								member_type_info->typeEnum());
						}
					}
				}
			}
				return TypeIndex{};
			};
			if (!owner_name.empty()) {
				for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
					const TemplateParameterNode* template_param =
						tryGetTemplateParameterNode(template_params[i]);
					if (template_param == nullptr ||
						StringTable::getStringView(template_param->nameHandle()) != owner_name ||
						template_args[i].is_value) {
						continue;
					}
					TypeIndex resolved = try_resolve_for_owner(tryGetTypeInfo(template_args[i].type_index));
					if (resolved.is_valid()) {
						return resolved;
					}
					break;
				}
			} else {
				const TypeInfo* unique_owner_type_info = nullptr;
				for (size_t i = 0; i < template_args.size(); ++i) {
					if (template_args[i].is_value) {
						continue;
					}
					const TypeInfo* candidate_owner_info =
						tryGetTypeInfo(template_args[i].type_index);
					if (candidate_owner_info == nullptr ||
						!is_struct_type(candidate_owner_info->typeEnum())) {
						continue;
					}
					if (unique_owner_type_info != nullptr) {
						unique_owner_type_info = nullptr;
						break;
					}
					unique_owner_type_info = candidate_owner_info;
				}
				if (unique_owner_type_info != nullptr) {
					TypeIndex resolved = try_resolve_for_owner(unique_owner_type_info);
					if (resolved.is_valid()) {
						return resolved;
					}
				}
			}
		}
	}
	if (dependent_record == nullptr || dependent_record->member_chain.empty()) {
		return substituted_type_index;
	}

	const TypeInfo* owner_type_info = tryGetTypeInfo(substituted_type_index);
	if (owner_type_info == nullptr || !is_struct_type(owner_type_info->typeEnum())) {
		owner_type_info = nullptr;
		if (dependent_record->owner_name.isValid()) {
			for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
				const TemplateParameterNode* template_param =
					tryGetTemplateParameterNode(template_params[i]);
				if (template_param == nullptr ||
					template_param->nameHandle() != dependent_record->owner_name ||
					template_args[i].is_value) {
					continue;
				}
				owner_type_info = tryGetTypeInfo(template_args[i].type_index);
				break;
			}
		}
		if (owner_type_info == nullptr || !is_struct_type(owner_type_info->typeEnum())) {
			return substituted_type_index;
		}
	}

	const TypeInfo* current_type_info = owner_type_info;
	std::string current_lookup_name(StringTable::getStringView(owner_type_info->name()));
	for (const auto& member : dependent_record->member_chain) {
		if (!member.name.isValid()) {
			return substituted_type_index;
		}

		if (member.has_template_arguments) {
			std::vector<TemplateTypeArg> concrete_template_args;
			concrete_template_args.reserve(member.template_arguments.size());
			for (const auto& arg_info : member.template_arguments) {
				TemplateTypeArg concrete_arg =
					materializeTemplateArg(arg_info, template_params, template_args);
				if (concrete_arg.is_dependent ||
					concrete_arg.dependent_name.isValid() ||
					concrete_arg.dependent_expr.has_value()) {
					return substituted_type_index;
				}
				if (!concrete_arg.is_value && concrete_arg.type_index.is_valid()) {
					if (const TypeInfo* concrete_type_info =
							tryGetTypeInfo(concrete_arg.type_index);
						concrete_type_info != nullptr) {
						concrete_arg.type_index =
							concrete_type_info->registeredTypeIndex().withCategory(
								concrete_type_info->typeEnum());
						concrete_arg.setCategory(concrete_type_info->typeEnum());
					}
				}
				concrete_template_args.push_back(std::move(concrete_arg));
			}

			const std::string_view current_name =
				StringTable::getStringView(current_type_info->name());
			if (current_name.empty()) {
				return substituted_type_index;
			}
			const std::string template_name = std::string(StringBuilder()
				.append(current_name)
				.append("::")
				.append(StringTable::getStringView(member.name))
				.commit());
			std::optional<ASTNode> instantiated_member_template =
				instantiate_class_template(
					template_name,
					std::span<const TemplateTypeArg>(
						concrete_template_args.data(),
						concrete_template_args.size()),
					false);
			if (!instantiated_member_template.has_value() ||
				!instantiated_member_template->is<StructDeclarationNode>()) {
				return substituted_type_index;
			}

			const StringHandle instantiated_name =
				instantiated_member_template->as<StructDeclarationNode>().name();
			std::string_view instantiated_name_view =
				StringTable::getStringView(instantiated_name);
			std::string qualified_instantiated_name;
			if (instantiated_name_view.find("::") == std::string_view::npos) {
				const size_t owner_sep = template_name.rfind("::");
				if (owner_sep != std::string::npos) {
					qualified_instantiated_name = std::string(StringBuilder()
						.append(std::string_view(template_name).substr(0, owner_sep))
						.append("::")
						.append(instantiated_name_view)
						.commit());
				}
			}
			const TypeInfo* instantiated_type_info = nullptr;
			if (!qualified_instantiated_name.empty()) {
				instantiated_type_info = findTypeByName(
					StringTable::getOrInternStringHandle(qualified_instantiated_name));
				if (instantiated_type_info != nullptr) {
					current_lookup_name = qualified_instantiated_name;
				}
			}
			if (instantiated_type_info == nullptr) {
				instantiated_type_info = findTypeByName(instantiated_name);
				current_lookup_name = std::string(instantiated_name_view);
			}
			current_type_info = instantiated_type_info;
		} else {
			if (current_lookup_name.empty()) {
				return substituted_type_index;
			}
			const std::string qualified_member_name = std::string(StringBuilder()
				.append(current_lookup_name)
				.append("::")
				.append(StringTable::getStringView(member.name))
				.commit());
			current_type_info = findTypeByName(
				StringTable::getOrInternStringHandle(qualified_member_name));
		}

		if (current_type_info == nullptr) {
			return substituted_type_index;
		}
	}

	TypeIndex resolved_type_index =
		current_type_info->registeredTypeIndex().withCategory(current_type_info->typeEnum());
	return resolved_type_index;
}

template <
	typename ParamContainer,
	typename ArgContainer,
	typename SubstituteAstFn,
	typename SubstituteTypeIndexFn>
TypeSpecifierNode buildSubstitutedTypeSpecifier(
	const TypeSpecifierNode& original_type_spec,
	const ASTNode& original_type_node,
	const Token& fallback_token,
	const ParamContainer& template_params,
	const ArgContainer& template_args,
	SubstituteAstFn&& substitute_ast,
	SubstituteTypeIndexFn&& substitute_type_index,
	const StructDeclarationNode* owner_decl,
	TypeIndex instantiated_owner_type_index,
	TypeIndex override_type_index,
	bool apply_bound_metadata_to_full_substitution,
	bool apply_resolved_index_to_full_substitution) {
	ASTNode full_substituted_node = substitute_ast(original_type_node, template_params, template_args);
	TypeIndex substituted_type_index = override_type_index.is_valid()
		? override_type_index
		: substitute_type_index(original_type_spec, template_params, template_args);
	if (owner_decl != nullptr) {
		substituted_type_index = resolveOwnerAliasTypeIndex(
			substitute_type_index,
			*owner_decl,
			original_type_spec,
			template_params,
			template_args,
			substituted_type_index);
	}
	if (instantiated_owner_type_index.is_valid()) {
		substituted_type_index = resolveSelfRefParamIndex(
			substituted_type_index,
			instantiated_owner_type_index);
	}
	substituted_type_index = resolveDependentMemberPlaceholderFromOwnerArtifact(
		original_type_node,
		original_type_spec,
		substituted_type_index);

	TypeSpecifierNode substituted_type = full_substituted_node.is<TypeSpecifierNode>()
		? full_substituted_node.as<TypeSpecifierNode>()
		: TypeSpecifierNode(
			  substituted_type_index.category(),
			  original_type_spec.qualifier(),
			  getSubstitutedTypeSizeBits(substituted_type_index),
			  fallback_token,
			  original_type_spec.cv_qualifier());
	if (substituted_type_index.is_valid() &&
		(apply_resolved_index_to_full_substitution ||
		 !full_substituted_node.is<TypeSpecifierNode>())) {
		substituted_type.set_type_index(substituted_type_index.withCategory(substituted_type_index.category()));
		substituted_type.set_category(substituted_type_index.category());
	}
	if (!full_substituted_node.is<TypeSpecifierNode>()) {
		for (const auto& ptr_level : original_type_spec.pointer_levels()) {
			substituted_type.add_pointer_level(ptr_level.cv_qualifier);
		}
		substituted_type.set_reference_qualifier(original_type_spec.reference_qualifier());
	}
	if (apply_bound_metadata_to_full_substitution ||
		!full_substituted_node.is<TypeSpecifierNode>()) {
		applyBoundTemplateArgMetadata(
			substituted_type,
			original_type_spec,
			template_params,
			template_args);
	}
	if (original_type_spec.has_function_signature()) {
		substituted_type.set_function_signature(original_type_spec.function_signature());
	} else if (auto signature = resolveTemplateFunctionPointerSignature(
				   original_type_spec,
				   substituted_type.type_index(),
				   template_params,
				   template_args)) {
		substituted_type.set_function_signature(*signature);
	}
	normalizeSubstitutedTypeSpec(substituted_type);
	return substituted_type;
}

template <typename TDest, typename TSource>
void appendLazyTemplateSequence(TDest& destination, const TSource& source) {
	for (const auto& value : source) {
		destination.push_back(value);
	}
}

inline void appendLazyTemplateSequence(
	InlineVector<ASTNode, 4>& destination,
	const InlineVector<TemplateParameterNode, 4>& source) {
	for (const auto& value : source) {
		destination.push_back(ASTNode::emplace_node<TemplateParameterNode>(value));
	}
}

template <typename TParams, typename TArgs>
LazyMemberFunctionInfo buildLazyNestedMemberFunctionInfo(
	const StructMemberFunctionDecl& mem_func,
	StringHandle class_template_name,
	StringHandle qualified_name,
	StringHandle member_function_name,
	bool is_constructor,
	bool is_destructor,
	const TParams& template_params,
	const TArgs& template_args,
	const TemplateEnvironmentSnapshot* outer_parent_snapshot) {
	LazyMemberFunctionInfo lazy_mem_info;
	auto& id = lazy_mem_info.identity;
	id.original_member_node = mem_func.function_declaration;
	id.template_owner_name = class_template_name;
	id.instantiated_owner_name = qualified_name;
	id.original_lookup_name = member_function_name;
	id.operator_kind = mem_func.operator_kind;
	id.is_operator = mem_func.operator_kind != OverloadableOperator::None;
	id.is_const_method = mem_func.is_const();
	id.cv_qualifier = mem_func.cv_qualifier;
	if (is_constructor)
		id.kind = DeferredMemberIdentity::Kind::Constructor;
	else if (is_destructor)
		id.kind = DeferredMemberIdentity::Kind::Destructor;
	else
		id.kind = DeferredMemberIdentity::Kind::Function;
	appendLazyTemplateSequence(lazy_mem_info.template_params, template_params);
	if (mem_func.function_declaration.is<ConstructorDeclarationNode>()) {
		appendLazyTemplateSequence(
			lazy_mem_info.template_params,
			mem_func.function_declaration.as<ConstructorDeclarationNode>().template_parameters());
	} else if (mem_func.function_declaration.is<TemplateFunctionDeclarationNode>()) {
		appendLazyTemplateSequence(
			lazy_mem_info.template_params,
			mem_func.function_declaration.as<TemplateFunctionDeclarationNode>().template_parameters());
	}
	appendLazyTemplateSequence(lazy_mem_info.template_args, template_args);
	lazy_mem_info.outer_template_environment_snapshot = buildTemplateEnvironmentSnapshotFromBindings(
		template_params,
		template_args,
		outer_parent_snapshot);
	lazy_mem_info.access = mem_func.access;
	lazy_mem_info.is_virtual = mem_func.is_virtual;
	lazy_mem_info.is_pure_virtual = mem_func.is_pure_virtual;
	lazy_mem_info.is_override = mem_func.is_override;
	lazy_mem_info.is_final = mem_func.is_final;
	return lazy_mem_info;
}

template <typename TParams, typename TArgs>
void registerNestedMemberFunctionsForLazy(
	const StructDeclarationNode& nested_struct,
	StructTypeInfo& nested_struct_info,
	StringHandle class_template_name,
	StringHandle qualified_name,
	const TParams& template_params,
	const TArgs& template_args,
	const TemplateEnvironmentSnapshot* outer_parent_snapshot,
	bool should_register_lazy_members) {
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
				template_args,
				outer_parent_snapshot);
			if (should_register_lazy_members) {
				LazyMemberInstantiationRegistry::getInstance().registerLazyMember(std::move(lazy_mem_info));
			}
		} else if (const FunctionDeclarationNode* func_decl = get_function_decl_node(mem_func.function_declaration)) {
			const DeclarationNode& decl = func_decl->decl_node();

			auto lazy_mem_info = buildLazyNestedMemberFunctionInfo(
				mem_func,
				class_template_name,
				qualified_name,
				decl.identifier_token().handle(),
				false,
				false,
				template_params,
				template_args,
				outer_parent_snapshot);

			if (should_register_lazy_members) {
				LazyMemberInstantiationRegistry::getInstance().registerLazyMember(std::move(lazy_mem_info));
			}

			// Set is_const/volatile_member_function on the node so propagateAstProperties derives cv_qualifier.
			{
				ASTNode fn_node = mem_func.function_declaration;
				if (auto* fn = get_function_decl_node_mut(fn_node)) {
					fn->set_is_const_member_function(mem_func.is_const());
					fn->set_is_volatile_member_function(mem_func.is_volatile());
				}
			}
			if (mem_func.operator_kind != OverloadableOperator::None) {
				nested_struct_info.addOperatorOverload(
					mem_func.operator_kind,
					mem_func.function_declaration,
					mem_func.access,
					mem_func.is_virtual,
					mem_func.is_pure_virtual,
					mem_func.is_override,
					mem_func.is_final);
			} else {
				nested_struct_info.addMemberFunction(
					decl.identifier_token().handle(),
					mem_func.function_declaration,
					mem_func.access,
					mem_func.is_virtual,
					mem_func.is_pure_virtual,
					mem_func.is_override,
					mem_func.is_final);
			}
			// cv_qualifier is now auto-derived by propagateAstProperties

			FLASH_LOG(Templates, Debug, "Registered lazy member function for nested type: ",
					  qualified_name, "::", decl.identifier_token().value());
		}
	}
}
