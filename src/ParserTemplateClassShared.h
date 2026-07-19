#pragma once

struct SourceMemberStructInfoIndexMaps;
void registerSourceMemberStructInfoIndex(
	SourceMemberStructInfoIndexMaps& index_maps,
	const ASTNode& source_member,
	size_t struct_info_index);

ASTNode rebindStaticMemberInitializerFunctionCalls(
	const ASTNode& node,
	const StructTypeInfo* struct_info,
	bool set_qualified_name);

// ---------------------------------------------------------------------------
// Structural-dependency helpers for TemplateTypeArg
// ---------------------------------------------------------------------------

/// Returns true when \p arg is still structurally dependent on unresolved
/// template parameters — i.e. it cannot yet be used as a concrete
/// instantiation key.
///
/// Covers:
///  • is_dependent flag (type arg that was never fully substituted)
///  • surviving dependent_name (belt-and-suspenders guard)
///  • dependent NTTP expressions that still carry AST-backed unresolved identity
///  • Auto / DeclTypeAuto placeholder categories
///
/// Does NOT include is_pack — call sites that need pack-deferral must check
/// arg.is_pack separately (e.g. explicitTemplateArgsRequireDeferredInstantiation).
inline bool templateArgIsStructurallyDependent(const TemplateTypeArg& arg) {
	if (arg.is_dependent || arg.dependent_name.isValid() || arg.dependent_expr.has_value())
		return true;
	const TypeCategory cat = arg.category();
	return cat == TypeCategory::Auto || cat == TypeCategory::DeclTypeAuto;
}

/// Returns true when *any* argument in \p args is structurally dependent.
/// Accepts InlineVector via its implicit operator std::span<const TemplateTypeArg>.
inline bool anyTemplateArgIsStructurallyDependent(std::span<const TemplateTypeArg> args) {
	for (const TemplateTypeArg& arg : args) {
		if (templateArgIsStructurallyDependent(arg))
			return true;
	}
	return false;
}

// ---------------------------------------------------------------------------

// Helper to build qualified name strings for template/member lookup
// Returns a StringHandle that can be passed to findTypeByName
inline StringHandle buildQualifiedName(std::string_view owner, std::string_view member) {
	return StringTable::getOrInternStringHandle(
		StringBuilder()
			.append(owner)
			.append("::")
			.append(member)
			.commit());
}

inline void normalizeSubstitutedTypeSpec(TypeSpecifierNode& type_spec) {
	const ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(type_spec.type_index());
	if (resolved_alias.type_index.is_valid()) {
		type_spec.set_type_index(resolved_alias.type_index.withCategory(resolved_alias.typeEnum()));
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
		InlineVector<size_t, 4> array_dimensions;
		array_dimensions.reserve(type_dimensions.size() + resolved_alias.array_dimensions.size());
		for (size_t dimension : type_dimensions) {
			array_dimensions.push_back(dimension);
		}
		for (size_t dimension : resolved_alias.array_dimensions) {
			array_dimensions.push_back(dimension);
		}
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
inline TypeIndex substituteTemplateParameterTypeIndex(
	TypeIndex original_type_index,
	const ParamContainer& template_params,
	const ArgContainer& template_args) {
	TypeIndex substituted_type_index = original_type_index;
	bool substituted = false;
	forEachNonPackTemplateParamArgBinding(
		template_params,
		template_args,
		[&](const TemplateParameterNode& param, const TemplateTypeArg& arg, size_t) {
			if (substituted ||
				param.kind() != TemplateParameterKind::Type ||
				!arg.isTypeArgument() ||
				!FlashCpp::equalTypeIndexIdentity(
					original_type_index,
					param.registered_type_index()) ||
				!FlashCpp::hasConcreteTemplateIdentity(arg.type_index)) {
				return;
			}
			substituted_type_index =
				FlashCpp::canonicalizeTemplateIdentityTypeIndex(arg.type_index);
			substituted = true;
		});
	return substituted_type_index;
}

template <typename ParamContainer, typename ArgContainer>
inline const TemplateTypeArg* findTemplateArgByRegisteredTypeIndex(
	TypeIndex type_index,
	const ParamContainer& template_params,
	const ArgContainer& template_args) {
	const TemplateTypeArg* matched_arg = nullptr;
	forEachNonPackTemplateParamArgBinding(
		template_params,
		template_args,
		[&](const TemplateParameterNode& param, const TemplateTypeArg& arg, size_t) {
			if (matched_arg == nullptr &&
				param.kind() == TemplateParameterKind::Type &&
				FlashCpp::equalTypeIndexIdentity(
					type_index,
					param.registered_type_index())) {
				matched_arg = &arg;
			}
		});
	return matched_arg;
}

template <typename ParamContainer, typename ArgContainer>
inline FunctionSignature substituteTemplateFunctionSignature(
	FunctionSignature signature,
	const ParamContainer& template_params,
	const ArgContainer& template_args) {
	signature.return_type_index = substituteTemplateParameterTypeIndex(
		signature.return_type_index,
		template_params,
		template_args);
	for (TypeIndex& parameter_type_index : signature.parameter_type_indices) {
		parameter_type_index = substituteTemplateParameterTypeIndex(
			parameter_type_index,
			template_params,
			template_args);
	}
	return signature;
}

template <typename ParamContainer, typename ArgContainer>
inline void materializeSubstitutedFunctionTypeMetadata(
	TypeSpecifierNode& substituted_type,
	const TypeSpecifierNode& original_type,
	const ParamContainer& template_params,
	const ArgContainer& template_args) {
	normalizeSubstitutedTypeSpec(substituted_type);
	const ResolvedAliasTypeInfo substituted_alias =
		resolveAliasTypeInfo(substituted_type.type_index());
	const TypeCategory substituted_category = substituted_alias.type_index.is_valid()
		? substituted_alias.typeEnum()
		: substituted_type.type_index().category();
	if (substituted_category != TypeCategory::FunctionPointer &&
		substituted_category != TypeCategory::MemberFunctionPointer) {
		return;
	}

	std::optional<FunctionSignature> signature;
	if (substituted_type.has_function_signature()) {
		signature = substituted_type.function_signature();
	} else if (substituted_alias.function_signature.has_value()) {
		signature = substituted_alias.function_signature;
	} else if (original_type.has_function_signature()) {
		signature = original_type.function_signature();
	} else {
		const ResolvedAliasTypeInfo original_alias =
			resolveAliasTypeInfo(original_type.type_index());
		if (original_alias.function_signature.has_value()) {
			signature = original_alias.function_signature;
		} else {
			const TypeIndex binding_type_index = original_alias.type_index.is_valid()
				? original_alias.type_index
				: original_type.type_index();
			if (const auto* arg = findTemplateArgByRegisteredTypeIndex(
					binding_type_index, template_params, template_args)) {
				signature = arg->function_signature;
			}
		}
	}
	if (!signature.has_value()) {
		throw InternalError(
			"Concrete function pointer type is missing canonical FunctionSignature metadata");
	}
	substituted_type.set_function_signature(substituteTemplateFunctionSignature(
		*signature, template_params, template_args));
}

inline std::optional<FunctionSignature> getCanonicalFunctionPointerSignature(
	const TypeSpecifierNode& type_spec) {
	const ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(type_spec.type_index());
	const TypeCategory resolved_category = resolved_alias.type_index.is_valid()
		? resolved_alias.typeEnum()
		: type_spec.type_index().category();
	if (resolved_category != TypeCategory::FunctionPointer &&
		resolved_category != TypeCategory::MemberFunctionPointer) {
		return std::nullopt;
	}
	if (!type_spec.has_function_signature()) {
		throw InternalError(
			"Concrete function pointer type is missing canonical FunctionSignature metadata");
	}
	return type_spec.function_signature();
}

template <typename ParamContainer, typename ArgContainer>
inline void applyBoundTemplateArgMetadata(
	TypeSpecifierNode& substituted_type,
	const TypeSpecifierNode& original_type,
	const ParamContainer& template_params,
	const ArgContainer& template_args) {

	StringHandle type_name_handle = getStructuredTypeName(original_type);
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
	if (!substituted_type.has_member_class() && arg->member_class_name.isValid()) {
		substituted_type.set_member_class_name(arg->member_class_name);
	}
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

template <typename ParamContainer, typename ArgContainer, typename InstantiateFn>
inline TypeIndex resolveDependentMemberTemplatePlaceholderFromConcreteOwner(
	const TypeSpecifierNode& original_type_spec,
	const ParamContainer& template_params,
	const ArgContainer& template_args,
	InstantiateFn&& instantiate_class_template,
	TypeIndex substituted_type_index);

template <typename ParamContainer, typename ArgContainer, typename InstantiateFn>
inline TypeIndex resolveDependentMemberTemplatePlaceholderFromConcreteOwnerArtifact(
	const ASTNode* original_type_node,
	const TypeSpecifierNode& original_type_spec,
	const ParamContainer& template_params,
	const ArgContainer& template_args,
	InstantiateFn&& instantiate_class_template,
	TypeIndex substituted_type_index);

template <typename ParserLike, typename ParamContainer, typename ArgContainer>
inline TypeIndex resolveDependentMemberTemplateSubstitutionArtifacts(
	ParserLike& parser,
	const ASTNode* original_type_node,
	const TypeSpecifierNode& original_type_spec,
	const ParamContainer& template_params,
	const ArgContainer& template_args,
	TypeIndex substituted_type_index,
	bool resolve_concrete_owner,
	bool resolve_concrete_owner_artifact,
	bool resolve_materialized_member_alias);

template <
	typename ParamContainer,
	typename ArgContainer,
	typename InstantiateFn,
	typename MaterializeMemberAliasFn>
inline TypeIndex resolveDependentMemberTemplateSubstitutionArtifacts(
	const ASTNode* original_type_node,
	const TypeSpecifierNode& original_type_spec,
	const ParamContainer& template_params,
	const ArgContainer& template_args,
	InstantiateFn&& instantiate_class_template,
	MaterializeMemberAliasFn&& materialize_member_alias,
	TypeIndex substituted_type_index,
	bool resolve_concrete_owner,
	bool resolve_concrete_owner_artifact,
	bool resolve_materialized_member_alias) {
	if (resolve_concrete_owner) {
		substituted_type_index =
			resolveDependentMemberTemplatePlaceholderFromConcreteOwner(
				original_type_spec,
				template_params,
				template_args,
				instantiate_class_template,
				substituted_type_index);
	}
	if (resolve_concrete_owner_artifact) {
		substituted_type_index =
			resolveDependentMemberTemplatePlaceholderFromConcreteOwnerArtifact(
				original_type_node,
				original_type_spec,
				template_params,
				template_args,
				instantiate_class_template,
				substituted_type_index);
	}
	if (resolve_materialized_member_alias) {
		if (const TypeInfo* materialized_member_alias =
				materialize_member_alias(
					original_type_spec,
					template_params,
					template_args)) {
			substituted_type_index =
				materialized_member_alias->registeredTypeIndex().withCategory(
					materialized_member_alias->typeEnum());
		}
	}
	return substituted_type_index;
}

template <typename ParserLike, typename ParamContainer, typename ArgContainer>
inline TypeIndex resolveDependentMemberTemplateSubstitutionArtifacts(
	ParserLike& parser,
	const ASTNode* original_type_node,
	const TypeSpecifierNode& original_type_spec,
	const ParamContainer& template_params,
	const ArgContainer& template_args,
	TypeIndex substituted_type_index,
	bool resolve_concrete_owner,
	bool resolve_concrete_owner_artifact,
	bool resolve_materialized_member_alias) {
	return resolveDependentMemberTemplateSubstitutionArtifacts(
		original_type_node,
		original_type_spec,
		template_params,
		template_args,
		[&parser](
			std::string_view template_name,
			std::span<const TemplateTypeArg> template_args,
			bool force_eager) {
			return parser.try_instantiate_class_template(
				template_name,
				template_args,
				force_eager);
		},
		[&parser](
			const TypeSpecifierNode& type_spec,
			const auto& params,
			const auto& args) {
			return parser.materializeInstantiatedMemberAliasTarget(
				type_spec,
				std::span<const TemplateParameterNode>(params.data(), params.size()),
				std::span<const TemplateTypeArg>(args.data(), args.size()));
		},
		substituted_type_index,
		resolve_concrete_owner,
		resolve_concrete_owner_artifact,
		resolve_materialized_member_alias);
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
			if (!member.name.isValid()) {
				qualified_member_name_builder.reset();
				return substituted_type_index;
			}
			if (member.has_template_arguments) {
				// Cannot instantiate a member template alias here (no template params/args
				// available in the non-template overload). Reset the in-progress builder
				// and return the best available type - the original dependent placeholder
				// if one exists, so downstream template-aware callers can finish resolution.
				qualified_member_name_builder.reset();
				return original_type_spec.type_index().is_valid()
					? original_type_spec.type_index()
					: substituted_type_index;
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
			qualified_member_name_builder.reset();
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
		qualified_member_name_builder.reset();
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

template <typename ArgContainer>
inline const TypeInfo* findUniqueStructOwnerTypeFromTemplateArgs(
	const ArgContainer& template_args) {
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
			return nullptr;
		}
		unique_owner_type_info = candidate_owner_info;
	}
	return unique_owner_type_info;
}

template <typename ParamContainer, typename ArgContainer, typename InstantiateFn>
inline TypeIndex resolveDependentMemberTemplatePlaceholderFromConcreteOwner(
	const TypeSpecifierNode& original_type_spec,
	const ParamContainer& template_params,
	const ArgContainer& template_args,
	InstantiateFn&& instantiate_class_template,
	TypeIndex substituted_type_index) {
	if (!substituted_type_index.is_valid()) {
		return substituted_type_index;
	}

	const TypeInfo* owner_type_info = tryGetTypeInfo(substituted_type_index);
	if (owner_type_info == nullptr || !is_struct_type(owner_type_info->typeEnum())) {
		return substituted_type_index;
	}

	const TypeInfo* original_type_info = nullptr;
	if (original_type_spec.type_index().is_valid()) {
		original_type_info = tryGetTypeInfo(original_type_spec.type_index());
	}
	const TypeInfo* template_source_type_info = original_type_info;
	if (template_source_type_info != nullptr &&
		!template_source_type_info->isTemplateInstantiation()) {
		const ResolvedAliasTypeInfo resolved_original_alias =
			resolveAliasTypeInfo(
				template_source_type_info->registeredTypeIndex().withCategory(
					template_source_type_info->typeEnum()));
		if (resolved_original_alias.terminal_type_info != nullptr) {
			template_source_type_info = resolved_original_alias.terminal_type_info;
		}
	}
	const TypeInfo::DependentQualifiedNameRecord* dependent_record =
		template_source_type_info != nullptr && template_source_type_info->isDependentPlaceholder()
			? template_source_type_info->dependentQualifiedName()
			: nullptr;
	if (dependent_record == nullptr || dependent_record->member_chain.empty()) {
		return substituted_type_index;
	}

	const TypeInfo* current_type_info = owner_type_info;
	for (const auto& member : dependent_record->member_chain) {
		if (!member.name.isValid()) {
			return substituted_type_index;
		}

		if (member.has_template_arguments) {
			InlineVector<TemplateTypeArg, 4> concrete_template_args;
			concrete_template_args.reserve(member.template_arguments.size());
			for (const auto& arg_info : member.template_arguments) {
				TemplateTypeArg concrete_arg =
					materializeTemplateArg(arg_info, template_params, template_args);
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
			const std::string_view member_name = StringTable::getStringView(member.name);
			const StringHandle template_name = buildQualifiedName(current_name, member_name);
			std::optional<ASTNode> instantiated_member_template =
				instantiate_class_template(
					StringTable::getStringView(template_name),
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
			current_type_info = findTypeByName(instantiated_name);
		} else {
			const std::string_view current_name =
				StringTable::getStringView(current_type_info->name());
			if (current_name.empty()) {
				return substituted_type_index;
			}
			const std::string_view member_name = StringTable::getStringView(member.name);
			current_type_info = findTypeByName(buildQualifiedName(current_name, member_name));
		}

		if (current_type_info == nullptr) {
			return substituted_type_index;
		}
	}

	return current_type_info->registeredTypeIndex().withCategory(current_type_info->typeEnum());
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
	const TypeInfo* template_source_type_info = original_type_info;
	if (template_source_type_info != nullptr &&
		!template_source_type_info->isTemplateInstantiation()) {
		const ResolvedAliasTypeInfo resolved_original_alias =
			resolveAliasTypeInfo(
				template_source_type_info->registeredTypeIndex().withCategory(
					template_source_type_info->typeEnum()));
		if (resolved_original_alias.terminal_type_info != nullptr) {
			template_source_type_info = resolved_original_alias.terminal_type_info;
		}
	}
	const TypeInfo::DependentQualifiedNameRecord* dependent_record =
		template_source_type_info != nullptr && template_source_type_info->isDependentPlaceholder()
			? template_source_type_info->dependentQualifiedName()
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
	if (template_source_type_info != nullptr) {
		std::string_view base_template_name =
			template_source_type_info->isTemplateInstantiation()
				? StringTable::getStringView(template_source_type_info->baseTemplateName())
				: std::string_view{};
		std::string_view owner_name;
		std::string_view member_template_name;
		const size_t owner_sep = base_template_name.rfind("::");
		if (owner_sep != std::string_view::npos) {
			owner_name = base_template_name.substr(0, owner_sep);
			member_template_name = base_template_name.substr(owner_sep + 2);
		} else {
			std::string_view original_type_name =
				StringTable::getStringView(template_source_type_info->name());
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
		if (dependent_record != nullptr &&
			dependent_record->owner_name.isValid()) {
			std::string_view dependent_owner_name =
				StringTable::getStringView(dependent_record->owner_name);
			if (size_t dependent_owner_sep = dependent_owner_name.rfind("::");
				dependent_owner_sep != std::string_view::npos) {
				if (member_template_name.empty()) {
					member_template_name =
						dependent_owner_name.substr(dependent_owner_sep + 2);
				}
				owner_name = dependent_owner_name.substr(0, dependent_owner_sep);
			}
		}
		if (!member_template_name.empty()) {
			auto try_resolve_for_owner = [&](const TypeInfo* owner_type_info) -> TypeIndex {
				if (owner_type_info != nullptr && is_struct_type(owner_type_info->typeEnum())) {
					InlineVector<TemplateTypeArg, 4> concrete_template_args;
					std::span<const TypeInfo::TemplateArgInfo> owner_template_args =
						(dependent_record != nullptr &&
						 !dependent_record->owner_template_arguments.empty())
							? std::span<const TypeInfo::TemplateArgInfo>(
								  dependent_record->owner_template_arguments.data(),
								  dependent_record->owner_template_arguments.size())
							: std::span<const TypeInfo::TemplateArgInfo>(
								  template_source_type_info->templateArgs().data(),
								  template_source_type_info->templateArgs().size());
					concrete_template_args.reserve(owner_template_args.size());
					for (const auto& arg_info : owner_template_args) {
						TemplateTypeArg concrete_arg =
							materializeTemplateArg(arg_info, template_params, template_args);
						if (concrete_arg.is_dependent ||
							concrete_arg.dependent_name.isValid() ||
							concrete_arg.dependent_expr.has_value()) {
							return substituted_type_index;
						}
						concrete_template_args.push_back(std::move(concrete_arg));
					}
					const std::string_view owner_name_view =
						StringTable::getStringView(owner_type_info->name());
					const StringHandle concrete_template_name =
						buildQualifiedName(owner_name_view, member_template_name);
					std::optional<ASTNode> instantiated_member_template =
						instantiate_class_template(
							StringTable::getStringView(concrete_template_name),
							std::span<const TemplateTypeArg>(
								concrete_template_args.data(),
								concrete_template_args.size()),
							false);
					if (instantiated_member_template.has_value() &&
						instantiated_member_template->is<StructDeclarationNode>()) {
						std::string_view instantiated_name =
							StringTable::getStringView(
								instantiated_member_template->as<StructDeclarationNode>().name());
						StringHandle qualified_instantiated_name;
						if (instantiated_name.find("::") == std::string_view::npos) {
							qualified_instantiated_name =
								buildQualifiedName(owner_name_view, instantiated_name);
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
								return findTypeByName(buildQualifiedName(instantiated_owner_name, terminal_member));
							};
							const TypeInfo* member_type_info =
								lookup_member_type(StringTable::getStringView(qualified_instantiated_name));
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
				const TypeInfo* unique_owner_type_info =
					findUniqueStructOwnerTypeFromTemplateArgs(template_args);
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
	std::string_view current_lookup_name = StringTable::getStringView(owner_type_info->name());
	for (const auto& member : dependent_record->member_chain) {
		if (!member.name.isValid()) {
			return substituted_type_index;
		}

		if (member.has_template_arguments) {
			InlineVector<TemplateTypeArg, 4> concrete_template_args;
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
			const std::string_view member_name = StringTable::getStringView(member.name);
			const StringHandle template_name = buildQualifiedName(current_name, member_name);
			std::optional<ASTNode> instantiated_member_template =
				instantiate_class_template(
					StringTable::getStringView(template_name),
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
			StringHandle qualified_instantiated_name;
			const std::string_view template_name_view = StringTable::getStringView(template_name);
			if (instantiated_name_view.find("::") == std::string_view::npos) {
				const size_t owner_sep = template_name_view.rfind("::");
				if (owner_sep != std::string_view::npos) {
					qualified_instantiated_name = buildQualifiedName(
						template_name_view.substr(0, owner_sep),
						instantiated_name_view);
				}
			}
			const TypeInfo* instantiated_type_info = nullptr;
			if (qualified_instantiated_name.isValid()) {
				instantiated_type_info = findTypeByName(qualified_instantiated_name);
				if (instantiated_type_info != nullptr) {
					current_lookup_name = StringTable::getStringView(qualified_instantiated_name);
				}
			}
			if (instantiated_type_info == nullptr) {
				instantiated_type_info = findTypeByName(instantiated_name);
				current_lookup_name = instantiated_name_view;
			}
			current_type_info = instantiated_type_info;
		} else {
			if (current_lookup_name.empty()) {
				return substituted_type_index;
			}
			const std::string_view member_name = StringTable::getStringView(member.name);
			current_type_info = findTypeByName(buildQualifiedName(current_lookup_name, member_name));
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
	const bool full_substitution_is_type =
		full_substituted_node.is<TypeSpecifierNode>();
	const bool substituted_index_is_concrete =
		substituted_type_index.is_valid() &&
		!typeIndexContainsDependentPlaceholder(substituted_type_index);
	if (substituted_type_index.is_valid() &&
		(!full_substitution_is_type ||
		 (apply_resolved_index_to_full_substitution &&
		  substituted_index_is_concrete))) {
		substituted_type.set_type_index(substituted_type_index);
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
	materializeSubstitutedFunctionTypeMetadata(
		substituted_type,
		original_type_spec,
		template_params,
		template_args);
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

inline void mergeMissingLazyOuterBindings(
	InlineVector<TemplateParameterNode, 4>& template_params,
	InlineVector<TemplateTypeArg, 4>& template_args,
	const TemplateEnvironmentSnapshot& outer_snapshot) {
	if (!hasTemplateEnvironmentSnapshotBindings(outer_snapshot)) {
		return;
	}

	InlineVector<StringHandle, 4> outer_param_names;
	InlineVector<TypeInfo::TemplateArgInfo, 4> outer_arg_infos;
	populateTemplateEnvironmentLegacyViews(
		outer_snapshot,
		outer_param_names,
		outer_arg_infos);

	InlineVector<TemplateParameterNode, 4> merged_params;
	InlineVector<TemplateTypeArg, 4> merged_args;
	merged_params.reserve(outer_param_names.size() + template_params.size());
	merged_args.reserve(outer_arg_infos.size() + template_args.size());

	for (size_t i = 0; i < outer_param_names.size() && i < outer_arg_infos.size(); ++i) {
		bool already_present = false;
		for (const TemplateParameterNode& template_param : template_params) {
			if (template_param.nameHandle() == outer_param_names[i]) {
				already_present = true;
				break;
			}
		}
		if (already_present) {
			continue;
		}
		const TemplateTypeArg outer_arg = toTemplateTypeArg(outer_arg_infos[i]);
		Token outer_token(
			Token::Type::Identifier,
			StringTable::getStringView(outer_param_names[i]),
			0,
			0,
			0);
		if (outer_arg.is_value) {
			TypeSpecifierNode nttp_type(
				outer_arg.type_index.withCategory(outer_arg.typeEnum()),
				TypeQualifier::None,
				get_type_size_bits(outer_arg.typeEnum()),
				outer_token,
				outer_arg.cv_qualifier);
			nttp_type.set_reference_qualifier(outer_arg.ref_qualifier);
			for (size_t ptr_i = 0; ptr_i < outer_arg.pointer_depth; ++ptr_i) {
				CVQualifier ptr_cv = CVQualifier::None;
				if (ptr_i < outer_arg.pointer_cv_qualifiers.size()) {
					ptr_cv = outer_arg.pointer_cv_qualifiers[ptr_i];
				}
				nttp_type.add_pointer_level(ptr_cv);
			}
			merged_params.push_back(
				TemplateParameterNode(
					outer_param_names[i],
					nttp_type,
					outer_token));
		} else {
			merged_params.push_back(TemplateParameterNode(outer_param_names[i], outer_token));
		}
		merged_args.push_back(outer_arg);
	}

	for (const TemplateParameterNode& template_param : template_params) {
		merged_params.push_back(template_param);
	}
	for (const TemplateTypeArg& template_arg : template_args) {
		merged_args.push_back(template_arg);
	}

	template_params = std::move(merged_params);
	template_args = std::move(merged_args);
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
	mergeMissingLazyOuterBindings(
		lazy_mem_info.template_params,
		lazy_mem_info.template_args,
		lazy_mem_info.outer_template_environment_snapshot);
	lazy_mem_info.access = mem_func.access;
	lazy_mem_info.is_virtual = mem_func.is_virtual;
	lazy_mem_info.is_pure_virtual = mem_func.is_pure_virtual;
	lazy_mem_info.is_override = mem_func.is_override;
	lazy_mem_info.is_final = mem_func.is_final;
	return lazy_mem_info;
}

template <typename MaterializeConstructorFn>
inline void addNestedMemberFunctionsToStructInfo(
	const StructDeclarationNode& nested_struct,
	StructTypeInfo& nested_struct_info,
	SourceMemberStructInfoIndexMaps* struct_info_index_maps,
	MaterializeConstructorFn&& materialize_constructor) {
	for (const StructMemberFunctionDecl& mem_func : nested_struct.member_functions()) {
		if (mem_func.is_constructor || mem_func.is_destructor) {
			if (mem_func.is_constructor) {
				nested_struct_info.addConstructor(
					materialize_constructor(mem_func),
					mem_func.access);
			} else {
				nested_struct_info.addDestructor(
					mem_func.function_declaration,
					mem_func.access,
					mem_func.is_virtual);
			}
			if (struct_info_index_maps != nullptr) {
				registerSourceMemberStructInfoIndex(
					*struct_info_index_maps,
					mem_func.function_declaration,
					nested_struct_info.member_functions.size() - 1);
			}
			continue;
		}

		const FunctionDeclarationNode* func_decl =
			get_function_decl_node(mem_func.function_declaration);
		if (func_decl == nullptr) {
			continue;
		}

		{
			ASTNode fn_node = mem_func.function_declaration;
			if (auto* fn = get_function_decl_node_mut(fn_node)) {
				fn->set_is_const_member_function(mem_func.is_const());
				fn->set_is_volatile_member_function(mem_func.is_volatile());
			}
		}

		const DeclarationNode& decl = func_decl->decl_node();
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
		if (struct_info_index_maps != nullptr) {
			registerSourceMemberStructInfoIndex(
				*struct_info_index_maps,
				mem_func.function_declaration,
				nested_struct_info.member_functions.size() - 1);
		}
	}
}

template <typename TParams, typename TArgs>
inline void registerNestedMemberFunctionsLazyEntries(
	const StructDeclarationNode& nested_struct,
	StringHandle class_template_name,
	StringHandle qualified_name,
	const TParams& template_params,
	const TArgs& template_args,
	const TemplateEnvironmentSnapshot* outer_parent_snapshot,
	bool should_register_lazy_members) {
	for (const StructMemberFunctionDecl& mem_func : nested_struct.member_functions()) {
		if (mem_func.is_constructor || mem_func.is_destructor) {
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

			FLASH_LOG(Templates, Debug, "Registered lazy member function for nested type: ",
					  qualified_name, "::", decl.identifier_token().value());
		}
	}
}

template <typename TParams, typename TArgs>
inline void registerNestedMemberFunctionsForLazy(
	const StructDeclarationNode& nested_struct,
	StructTypeInfo& nested_struct_info,
	StringHandle class_template_name,
	StringHandle qualified_name,
	const TParams& template_params,
	const TArgs& template_args,
	const TemplateEnvironmentSnapshot* outer_parent_snapshot,
	bool should_register_lazy_members) {
	addNestedMemberFunctionsToStructInfo(
		nested_struct,
		nested_struct_info,
		nullptr,
		[](const StructMemberFunctionDecl& mem_func) {
			return mem_func.function_declaration;
		});
	registerNestedMemberFunctionsLazyEntries(
		nested_struct,
		class_template_name,
		qualified_name,
		template_params,
		template_args,
		outer_parent_snapshot,
		should_register_lazy_members);
}

// ---------------------------------------------------------------------------
// Shared template instantiation helpers
// ---------------------------------------------------------------------------

inline ReferenceQualifier collapseTemplateArgumentReferenceQualifier(
	ReferenceQualifier original_ref_qualifier,
	ReferenceQualifier template_arg_ref_qualifier) {
	if (template_arg_ref_qualifier == ReferenceQualifier::None) {
		return original_ref_qualifier;
	}
	if (original_ref_qualifier == ReferenceQualifier::None) {
		return template_arg_ref_qualifier;
	}
	if (original_ref_qualifier == ReferenceQualifier::LValueReference ||
		template_arg_ref_qualifier == ReferenceQualifier::LValueReference) {
		return ReferenceQualifier::LValueReference;
	}
	return ReferenceQualifier::RValueReference;
}

template <typename ParamContainer, typename ArgContainer>
inline void applyTemplateArgIndirection(
	TypeSpecifierNode& substituted_type,
	const TypeSpecifierNode& orig_type,
	const ParamContainer& template_params,
	const ArgContainer& template_args,
	bool propagate_reference_qualifier) {
	std::string_view type_name = orig_type.token().value();
	if (type_name.empty()) {
		if (const TypeInfo* type_info = tryGetTypeInfo(orig_type.type_index())) {
			type_name = StringTable::getStringView(type_info->name());
		}
	}
	if (const auto* arg = findTemplateArgByName(type_name, template_params, template_args)) {
		for (size_t pd = 0; pd < arg->pointer_depth; ++pd) {
			CVQualifier cv = pd < arg->pointer_cv_qualifiers.size() ? arg->pointer_cv_qualifiers[pd] : CVQualifier::None;
			substituted_type.add_pointer_level(cv);
		}
		if (propagate_reference_qualifier && arg->ref_qualifier != ReferenceQualifier::None) {
			substituted_type.set_reference_qualifier(
				collapseTemplateArgumentReferenceQualifier(
					substituted_type.reference_qualifier(),
					arg->ref_qualifier));
		}
		if (!substituted_type.is_array() && arg->is_array) {
			if constexpr (requires(decltype(*arg) a) { a.array_size(); }) {
				substituted_type.set_array_dimensions(arg->array_dimensions);
			} else {
				substituted_type.set_array(true, arg->array_size);
			}
		}
	}
}

inline TemplateParameterNode rebuildOuterTemplateParameter(
	StringHandle param_name,
	const TypeInfo::TemplateArgInfo& arg_info) {
	Token outer_token(
		Token::Type::Identifier,
		StringTable::getStringView(param_name),
		0,
		0,
		0);
	if (arg_info.is_template_template_arg) {
		return TemplateParameterNode(
			param_name,
			std::vector<TemplateParameterNode>{},
			outer_token);
	}
	if (auto outer_type_spec = makeTypeSpecifierFromTemplateArgInfo(arg_info, outer_token);
		outer_type_spec.has_value()) {
		return TemplateParameterNode(param_name, *outer_type_spec, outer_token);
	}
	return TemplateParameterNode(param_name, outer_token);
}

inline TemplateParameterNode rebuildOuterTemplateParameter(
	StringHandle param_name,
	const TemplateTypeArg& arg) {
	Token outer_token(
		Token::Type::Identifier,
		StringTable::getStringView(param_name),
		0,
		0,
		0);
	if (arg.is_template_template_arg) {
		return TemplateParameterNode(
			param_name,
			std::vector<TemplateParameterNode>{},
			outer_token);
	}
	if (arg.is_value) {
		return TemplateParameterNode(
			param_name,
			makeTypeSpecifierFromTemplateTypeArg(arg, outer_token),
			outer_token);
	}
	TemplateParameterNode outer_param(param_name, outer_token);
	outer_param.set_registered_type_index(
		arg.type_index.withCategory(arg.typeEnum()));
	return outer_param;
}
