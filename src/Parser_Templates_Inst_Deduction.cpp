#include "Parser.h"
#include "AstTraversal.h"
#include "ConstExprEvaluator.h"
#include "ExpressionSubstitutor.h"
#include "NameMangling.h"
#include "OverloadResolution.h"
#include "ParserTemplateClassShared.h"
#include "TemplateRegistry_Pattern.h"
#include "TypeTraitEvaluator.h"

#include <numeric>

template <typename ParamContainer>
static bool defaultExpressionReferencesTemplateParams(
	const ASTNode& expr,
	const ParamContainer& template_params) {
	InlineVector<StringHandle, 4> param_names;
	for (const auto& param : template_params) {
		param_names.push_back(param.nameHandle());
	}

	auto is_template_param = [&](StringHandle name) {
		return std::find(param_names.begin(), param_names.end(), name) != param_names.end();
	};

	return AstTraversal::visitASTUntil(expr, [&](const ASTNode& current) {
		if (current.is<TemplateParameterReferenceNode>()) {
			return true;
		}
		if (current.is<IdentifierNode>()) {
			return is_template_param(current.as<IdentifierNode>().nameHandle());
		}
		if (current.is<TypeSpecifierNode>()) {
			return is_template_param(current.as<TypeSpecifierNode>().token().handle());
		}
		if (current.is<QualifiedIdentifierNode>()) {
			const QualifiedIdentifierNode& qualified_identifier = current.as<QualifiedIdentifierNode>();
			std::string_view namespace_name =
				gNamespaceRegistry.getQualifiedName(qualified_identifier.namespace_handle());
			if (namespace_name.empty()) {
				return false;
			}
			auto type_it = getTypesByNameMap().find(
				StringTable::getOrInternStringHandle(namespace_name));
			return type_it != getTypesByNameMap().end() &&
				   type_it->second != nullptr &&
				   (type_it->second->isDependentPlaceholder() ||
					type_it->second->is_incomplete_instantiation_);
		}
		return false;
	});
}

template <typename ParamContainer, typename ArgContainer>
static void propagateFunctionSignatureFromTemplateArg(
	TypeSpecifierNode& substituted_type,
	const TypeSpecifierNode& orig_type,
	TypeIndex substituted_type_index,
	const ParamContainer& template_params,
	const ArgContainer& template_args) {
	if (orig_type.has_function_signature()) {
		substituted_type.set_function_signature(orig_type.function_signature());
	} else if (substituted_type_index.category() == TypeCategory::FunctionPointer ||
			   substituted_type_index.category() == TypeCategory::MemberFunctionPointer) {
		if (const auto* arg = findTemplateArgByName(
				orig_type.token().value(), template_params, template_args)) {
			if (arg->function_signature.has_value()) {
				substituted_type.set_function_signature(*arg->function_signature);
			}
		}
	}
}

static ReferenceQualifier collapseTemplateArgumentReferenceQualifier(
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

bool Parser::isTemplateFunctionParameterPack(
	std::span<const TemplateParameterNode> template_params,
	const DeclarationNode& func_param_decl) {
	if (func_param_decl.is_parameter_pack()) {
		return true;
	}

	std::unordered_map<StringHandle, const TemplateParameterNode*, StringHash, StringEqual> tparam_nodes_by_name;
	for (const TemplateParameterNode& template_param : template_params) {
		tparam_nodes_by_name.emplace(template_param.nameHandle(), &template_param);
	}

	const TypeSpecifierNode& fp_ts = func_param_decl.type_specifier_node();
	if (fp_ts.category() != TypeCategory::UserDefined &&
		fp_ts.category() != TypeCategory::TypeAlias &&
		fp_ts.category() != TypeCategory::Template) {
		return false;
	}

	StringHandle fp_type_name;
	if (const TypeInfo* ti = tryGetTypeInfo(fp_ts.type_index())) {
		fp_type_name = ti->name();
	}
	if (!fp_type_name.isValid()) {
		fp_type_name = fp_ts.token().handle();
	}
	if (!fp_type_name.isValid()) {
		return false;
	}

	auto it = tparam_nodes_by_name.find(fp_type_name);
	if (it != tparam_nodes_by_name.end() && it->second->is_variadic()) {
		return true;
	}

	std::unordered_set<StringHandle, StringHash, StringEqual> dependent_param_names;
	StringHandle primary_name;
	collectDependentTemplateParamNamesFromType(
		fp_ts.type_index(),
		tparam_nodes_by_name,
		primary_name,
		dependent_param_names);
	for (StringHandle dep_name : dependent_param_names) {
		auto dep_it = tparam_nodes_by_name.find(dep_name);
		if (dep_it != tparam_nodes_by_name.end() && dep_it->second->is_variadic()) {
			return true;
		}
	}

	return false;
}

static void applyRegisteredTypeBindingMetadata(
	TypeInfo& type_info,
	const TemplateTypeArg& arg,
	bool preserve_ref_qualifier) {
	if (is_builtin_type(arg.typeEnum())) {
		type_info.fallback_size_bits_ = get_type_size_bits(arg.category());
	} else if (const TypeInfo* arg_type_info = tryGetTypeInfo(arg.type_index)) {
		type_info.fallback_size_bits_ = arg_type_info->sizeInBits().value;
	} else {
		type_info.fallback_size_bits_ = 0;
	}

	if (preserve_ref_qualifier) {
		type_info.reference_qualifier_ = arg.is_rvalue_reference()
											 ? ReferenceQualifier::RValueReference
											 : (arg.is_lvalue_reference() ? ReferenceQualifier::LValueReference : ReferenceQualifier::None);
	}
}

static TypeInfo& registerTemplateTypeBinding(
	StringHandle param_name,
	const TemplateTypeArg& arg) {
	TypeIndex registered_type_index = arg.type_index;
	if (registered_type_index.is_valid()) {
		registered_type_index = registered_type_index.withCategory(arg.typeEnum());
	} else {
		registered_type_index = nativeTypeIndex(arg.typeEnum());
		if (!registered_type_index.is_valid()) {
			registered_type_index = TypeIndex{0, arg.typeEnum()};
		}
	}
	TypeSpecifierNode alias_spec = makeTypeSpecifierFromTemplateTypeArg(arg, Token());
	alias_spec.set_type_index(registered_type_index.withCategory(arg.typeEnum()));
	return add_type_alias_copy(
		param_name,
		registered_type_index.withCategory(arg.typeEnum()),
		getTypeSizeFromTemplateArgument(arg),
		alias_spec);
}

static void resetTypeIndirection(TypeSpecifierNode& type_spec) {
	const TypeSpecifierNode empty_spec;
	type_spec.copy_indirection_from(empty_spec);
}

static void mergeAliasAndUseSiteTypeSpec(
	TypeSpecifierNode& resolved_spec,
	const TypeSpecifierNode& use_site_spec,
	const TypeSpecifierNode* alias_type_spec) {
	resolved_spec.set_cv_qualifier(use_site_spec.cv_qualifier());
	resetTypeIndirection(resolved_spec);
	if (alias_type_spec) {
		resolved_spec.add_cv_qualifier(alias_type_spec->cv_qualifier());
		for (const auto& pointer_level : alias_type_spec->pointer_levels()) {
			resolved_spec.add_pointer_level(pointer_level.cv_qualifier);
		}
		if (alias_type_spec->reference_qualifier() != ReferenceQualifier::None) {
			resolved_spec.set_reference_qualifier(alias_type_spec->reference_qualifier());
		}
		if (alias_type_spec->is_array()) {
			resolved_spec.set_array_dimensions(alias_type_spec->array_dimensions());
		}
		if (alias_type_spec->has_function_signature()) {
			resolved_spec.set_function_signature(alias_type_spec->function_signature());
		}
	}
	for (const auto& pointer_level : use_site_spec.pointer_levels()) {
		resolved_spec.add_pointer_level(pointer_level.cv_qualifier);
	}
	if (use_site_spec.reference_qualifier() != ReferenceQualifier::None || resolved_spec.reference_qualifier() != ReferenceQualifier::None) {
		resolved_spec.set_reference_qualifier(
			collapseTemplateArgumentReferenceQualifier(
				resolved_spec.reference_qualifier(),
				use_site_spec.reference_qualifier()));
	}
	if (use_site_spec.is_array()) {
		resolved_spec.set_array_dimensions(use_site_spec.array_dimensions());
	}
	if (use_site_spec.has_function_signature()) {
		resolved_spec.set_function_signature(use_site_spec.function_signature());
	}
}

Parser::DependentAliasResolutionStatus Parser::resolveDependentMemberAlias(
	ASTNode& type_node,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args) {
	if (!type_node.is<TypeSpecifierNode>())
		return DependentAliasResolutionStatus::NotApplicable;
	auto& ts = type_node.as<TypeSpecifierNode>();
	if (ts.category() != TypeCategory::UserDefined &&
		ts.category() != TypeCategory::TypeAlias &&
		ts.category() != TypeCategory::Template)
		return DependentAliasResolutionStatus::NotApplicable;
	TypeIndex idx = ts.type_index();
	const TypeInfo* type_info = tryGetTypeInfo(idx);
	if (!type_info)
		return DependentAliasResolutionStatus::StillDependent;

	std::string_view type_name = StringTable::getStringView(type_info->name());
	auto emplaceResolvedSpec = [&](const TypeInfo* resolved_info) {
		ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(resolved_info->registeredTypeIndex());
		TypeIndex resolved_type_index = resolved_alias.type_index.is_valid()
			? resolved_alias.type_index.withCategory(resolved_alias.typeEnum())
			: resolved_info->registeredTypeIndex();
		int resolved_size_bits = resolved_info->hasStoredSize()
			? static_cast<int>(resolved_info->sizeInBits().value)
			: get_type_size_bits(resolved_type_index.category());
		TypeSpecifierNode resolved_spec(
			resolved_type_index,
			resolved_size_bits,
			ts.token(),
			CVQualifier::None,
			ReferenceQualifier::None);
		mergeAliasAndUseSiteTypeSpec(resolved_spec, ts, resolved_info->aliasTypeSpecifier());
		type_node = emplace_node<TypeSpecifierNode>(resolved_spec);
		return DependentAliasResolutionStatus::Resolved;
	};

	if (type_info->isDependentMemberType() &&
		type_info->hasDependentQualifiedName()) {
		if (const TypeInfo* resolved_dependent_type =
				resolveDependentMemberTypeSemantic(
					*type_info,
					template_params,
					template_args,
					StringHandle{});
			resolved_dependent_type != nullptr) {
			return emplaceResolvedSpec(resolved_dependent_type);
		}
	}

	if (const StructTypeInfo* owner_struct = type_info->getStructInfo();
		owner_struct && type_name.find("::") == std::string_view::npos) {
		std::string_view token_name = ts.token().value();
		std::string_view owner_name = StringTable::getStringView(owner_struct->name);
		if (!token_name.empty() && token_name != owner_name) {
			StringHandle qualified_alias_handle = StringTable::getOrInternStringHandle(
				StringBuilder().append(owner_struct->name).append("::").append(token_name).commit());
			auto qualified_type_it = getTypesByNameMap().find(qualified_alias_handle);
			if (qualified_type_it != getTypesByNameMap().end() &&
				qualified_type_it->second != nullptr &&
				!qualified_type_it->second->is_incomplete_instantiation_) {
				return emplaceResolvedSpec(qualified_type_it->second);
			}
		}
	}

	if (auto direct_alias = gTemplateRegistry.lookup_alias_template(std::string(type_name));
		direct_alias.has_value() && direct_alias->is<TemplateAliasNode>()) {
		const auto& alias_node = direct_alias->as<TemplateAliasNode>();
		type_node = substituteTemplateParameters(
			ASTNode(&alias_node.target_type()),
			template_params,
			template_args);
		if (!type_node.is<TypeSpecifierNode>())
			return DependentAliasResolutionStatus::StillDependent;
		idx = type_node.as<TypeSpecifierNode>().type_index();
		type_info = tryGetTypeInfo(idx);
		if (!type_info)
			return DependentAliasResolutionStatus::StillDependent;
		type_name = StringTable::getStringView(type_info->name());
		FLASH_LOG(Templates, Debug, "Resolved dependent alias through substitution: ", type_name);
	}

	auto sep_pos = type_name.find("::");
	if (sep_pos == std::string_view::npos)
		return DependentAliasResolutionStatus::NotApplicable;

	std::string base_part(type_name.substr(0, sep_pos));
	std::string_view member_part = type_name.substr(sep_pos + 2);
	auto build_resolved_handle = [](std::string_view base, std::string_view member) {
		StringBuilder sb;
		return StringTable::getOrInternStringHandle(sb.append(base).append("::").append(member).commit());
	};
	auto instantiate_base_and_get_name = [&](std::string_view base_template_name,
										   std::span<const TemplateTypeArg> args_to_use) -> std::string_view {
		auto instantiation = try_instantiate_class_template(base_template_name, args_to_use);
		if (instantiation.has_value() && instantiation->is<StructDeclarationNode>()) {
			return StringTable::getStringView(instantiation->as<StructDeclarationNode>().name());
		}
		auto registry_hit = gTemplateRegistry.getInstantiation(
			StringTable::getOrInternStringHandle(base_template_name), args_to_use);
		if (registry_hit.has_value() && registry_hit->is<StructDeclarationNode>()) {
			return StringTable::getStringView(registry_hit->as<StructDeclarationNode>().name());
		}
		return get_instantiated_class_name(base_template_name, args_to_use);
	};

	FLASH_LOG(Templates, Debug, "resolveDependentMemberAlias: type_name=", type_name,
			  " base_part=", base_part, " member_part=", member_part,
			  " template_args=", template_args.size());

	forEachNonPackTemplateParamArgBinding(
		template_params,
		template_args,
		[&](const TemplateParameterNode& tparam, const TemplateTypeArg& arg, size_t) {
			std::string_view tname = tparam.name();
			auto pos = base_part.find(tname);
			if (pos != std::string::npos) {
				base_part.replace(pos, tname.size(), arg.toString());
			}
		});

	StringHandle resolved_handle = build_resolved_handle(base_part, member_part);
	FLASH_LOG(Templates, Debug, "resolveDependentMemberAlias: resolved_name=",
			  StringTable::getStringView(resolved_handle));
	auto type_it = getTypesByNameMap().find(resolved_handle);
	if (type_it != getTypesByNameMap().end() &&
		type_it->second != nullptr &&
		type_it->second->is_incomplete_instantiation_) {
		type_it = getTypesByNameMap().end();
	}

	std::vector<TemplateTypeArg> concrete_instantiation_args;
	if (type_info->isTemplateInstantiation()) {
		concrete_instantiation_args =
			materializePlaceholderTemplateArgs(*type_info, template_params, template_args);
	}
	if (concrete_instantiation_args.empty()) {
		auto base_type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(base_part));
		if (base_type_it != getTypesByNameMap().end() &&
			base_type_it->second != nullptr &&
			base_type_it->second->isTemplateInstantiation()) {
			concrete_instantiation_args =
				materializePlaceholderTemplateArgs(*base_type_it->second, template_params, template_args);
		}
	}
	if (type_it == getTypesByNameMap().end() && type_info->isTemplateInstantiation()) {
		std::string_view base_template_name = StringTable::getStringView(type_info->baseTemplateName());
		if (!base_template_name.empty()) {
			std::string_view instantiated_base =
				instantiate_base_and_get_name(base_template_name, concrete_instantiation_args);
			resolved_handle = build_resolved_handle(instantiated_base, member_part);
			type_it = getTypesByNameMap().find(resolved_handle);
			if (type_it != getTypesByNameMap().end() &&
				type_it->second != nullptr &&
				type_it->second->is_incomplete_instantiation_) {
				type_it = getTypesByNameMap().end();
			}
		}
	}

	auto resolve_base_template_name = [&](std::string_view candidate) {
		std::string_view base_template_name = extractBaseTemplateName(candidate);
		if (base_template_name.empty()) {
			base_template_name = extract_base_template_name(candidate);
		}
		return base_template_name;
	};
	if (type_it == getTypesByNameMap().end()) {
		std::string_view base_template_name = resolve_base_template_name(base_part);
		if (!base_template_name.empty()) {
			auto template_opt = gTemplateRegistry.lookupTemplate(base_template_name);
			if (template_opt.has_value() && template_opt->is<TemplateClassDeclarationNode>()) {
				std::vector<TemplateTypeArg> args_to_use = concrete_instantiation_args.empty()
					? std::vector<TemplateTypeArg>(template_args.begin(), template_args.end())
					: concrete_instantiation_args;
				std::string_view instantiated_base =
					instantiate_base_and_get_name(base_template_name, args_to_use);
				resolved_handle = build_resolved_handle(instantiated_base, member_part);
				type_it = getTypesByNameMap().find(resolved_handle);
				if (type_it != getTypesByNameMap().end() &&
					type_it->second != nullptr &&
					type_it->second->is_incomplete_instantiation_) {
					type_it = getTypesByNameMap().end();
				}
				if (type_it == getTypesByNameMap().end()) {
					StringHandle primary_handle = build_resolved_handle(base_template_name, member_part);
					type_it = getTypesByNameMap().find(primary_handle);
					if (type_it != getTypesByNameMap().end() &&
						type_it->second != nullptr &&
						type_it->second->is_incomplete_instantiation_) {
						type_it = getTypesByNameMap().end();
					}
				}
				FLASH_LOG(Templates, Debug, "resolveDependentMemberAlias: after instantiation lookup '",
						  StringTable::getStringView(resolved_handle), "' found=", (type_it != getTypesByNameMap().end()));
			}
		}
	}

	if (type_it == getTypesByNameMap().end()) {
		if (type_info->isTemplateInstantiation()) {
			std::string_view base_template_name = StringTable::getStringView(type_info->baseTemplateName());
			if (!base_template_name.empty()) {
				std::vector<TemplateTypeArg> args_to_use = concrete_instantiation_args.empty()
					? std::vector<TemplateTypeArg>(template_args.begin(), template_args.end())
					: concrete_instantiation_args;
				auto instantiation_opt = gTemplateRegistry.getInstantiation(
					StringTable::getOrInternStringHandle(base_template_name),
					args_to_use);
				if (instantiation_opt.has_value() && instantiation_opt->is<StructDeclarationNode>()) {
					const auto& instantiated_struct = instantiation_opt->as<StructDeclarationNode>();
					for (const auto& type_alias : instantiated_struct.type_aliases()) {
						if (StringTable::getStringView(type_alias.alias_name) != member_part) {
							continue;
						}
						StringHandle qualified_alias_handle = StringTable::getOrInternStringHandle(
							StringBuilder()
								.append(StringTable::getStringView(instantiated_struct.name()))
								.append("::")
								.append(member_part)
								.commit());
						auto qualified_alias_it = getTypesByNameMap().find(qualified_alias_handle);
						if (qualified_alias_it != getTypesByNameMap().end() && qualified_alias_it->second != nullptr) {
							FLASH_LOG(Templates, Debug, "Resolved dependent alias from registered qualified alias '",
									  StringTable::getStringView(qualified_alias_handle), "'");
							return emplaceResolvedSpec(qualified_alias_it->second);
						}
						if (type_alias.type_node.is<TypeSpecifierNode>()) {
							type_node = emplace_node<TypeSpecifierNode>(type_alias.type_node.as<TypeSpecifierNode>());
							FLASH_LOG(Templates, Debug, "Resolved dependent alias from instantiated struct '",
									  type_name, "' -> ", member_part);
							return DependentAliasResolutionStatus::Resolved;
						}
					}
				}
			}
		}
		auto alias_opt = gTemplateRegistry.lookup_alias_template(StringTable::getStringView(resolved_handle));
		if (alias_opt.has_value() && alias_opt->is<TemplateAliasNode>()) {
			const TemplateAliasNode& alias_node = alias_opt->as<TemplateAliasNode>();
			type_node = emplace_node<TypeSpecifierNode>(alias_node.target_type());
			FLASH_LOG(Templates, Debug, "Resolved dependent alias via registry '", type_name, "' -> ", alias_node.alias_name());
			return DependentAliasResolutionStatus::Resolved;
		}
		return DependentAliasResolutionStatus::StillDependent;
	}
	auto status = emplaceResolvedSpec(type_it->second);
	FLASH_LOG(Templates, Debug, "Resolved dependent alias '", type_name, "' to type=", static_cast<int>(type_it->second->typeEnum()),
			  ", index=", type_it->second->type_index_);
	return status;
}

const TypeInfo* Parser::resolveDependentMemberTypeSemantic(
	const TypeInfo& dependent_type_info,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args,
	StringHandle current_owner_type_name) {
	if (!dependent_type_info.isDependentMemberType() ||
		!dependent_type_info.hasDependentQualifiedName()) {
		return nullptr;
	}

	SubstitutionParamMap sub_map =
		buildSubstitutionParamMap(template_params, template_args);
	ExpressionSubstitutor substitutor(sub_map.param_map, *this, sub_map.param_order);
	if (current_owner_type_name.isValid()) {
		substitutor.setCurrentOwnerTypeName(current_owner_type_name);
	}
	return substitutor.resolveDependentMemberTypeForSubstitution(dependent_type_info);
}

static bool hasUsableTemplateFunctionDefinition(const FunctionDeclarationNode& func_decl) {
	return func_decl.has_template_body_position() ||
		   func_decl.is_materialized() ||
		   func_decl.is_deleted();
}

static bool hasTemplateFunctionBodyDefinition(const FunctionDeclarationNode& func_decl) {
	return func_decl.has_template_body_position() ||
		   func_decl.is_materialized();
}

static bool sameTypeSpecifierShape(const TypeSpecifierNode& lhs, const TypeSpecifierNode& rhs) {
	if (lhs.category() != rhs.category() ||
		lhs.cv_qualifier() != rhs.cv_qualifier() ||
		lhs.reference_qualifier() != rhs.reference_qualifier() ||
		lhs.pointer_levels().size() != rhs.pointer_levels().size() ||
		lhs.is_array() != rhs.is_array()) {
		return false;
	}
	for (size_t i = 0; i < lhs.pointer_levels().size(); ++i) {
		if (lhs.pointer_levels()[i].cv_qualifier != rhs.pointer_levels()[i].cv_qualifier) {
			return false;
		}
	}
	if (!std::ranges::equal(lhs.array_dimensions(), rhs.array_dimensions())) {
		return false;
	}
	if (lhs.has_function_signature() != rhs.has_function_signature()) {
		return false;
	}
	if (lhs.has_function_signature()) {
		const FunctionSignature& lhs_sig = lhs.function_signature();
		const FunctionSignature& rhs_sig = rhs.function_signature();
		if (lhs_sig.return_type_index != rhs_sig.return_type_index ||
			lhs_sig.return_pointer_depth != rhs_sig.return_pointer_depth ||
			lhs_sig.return_reference_qualifier != rhs_sig.return_reference_qualifier ||
			lhs_sig.parameter_type_indices != rhs_sig.parameter_type_indices ||
			lhs_sig.linkage != rhs_sig.linkage ||
			lhs_sig.class_name != rhs_sig.class_name ||
			lhs_sig.calling_convention != rhs_sig.calling_convention ||
			lhs_sig.is_const != rhs_sig.is_const ||
			lhs_sig.is_volatile != rhs_sig.is_volatile ||
			lhs_sig.function_reference_qualifier != rhs_sig.function_reference_qualifier ||
			lhs_sig.is_noexcept != rhs_sig.is_noexcept) {
			return false;
		}
	}
	TypeIndex lhs_type_index = lhs.type_index();
	TypeIndex rhs_type_index = rhs.type_index();
	if (lhs_type_index.needsTypeIndex() != rhs_type_index.needsTypeIndex()) {
		return false;
	}
	if (lhs_type_index.needsTypeIndex() && rhs_type_index.needsTypeIndex() &&
		lhs_type_index != rhs_type_index &&
		lhs.token().value() != rhs.token().value()) {
		return false;
	}
	return true;
}

template <typename LeftParamContainer, typename RightParamContainer>
static bool templateParameterListsHaveMatchingShape(const LeftParamContainer& lhs, const RightParamContainer& rhs) {
	auto same_shape = [&](const auto& self, const auto& lhs_params, const auto& rhs_params) -> bool {
		if (lhs_params.size() != rhs_params.size()) {
			return false;
		}
		for (size_t i = 0; i < lhs_params.size(); ++i) {
			const TemplateParameterNode* lhs_param_ptr = tryGetTemplateParameterNode(lhs_params[i]);
			const TemplateParameterNode* rhs_param_ptr = tryGetTemplateParameterNode(rhs_params[i]);
			if (lhs_param_ptr == nullptr || rhs_param_ptr == nullptr) {
				return false;
			}
			const TemplateParameterNode& lhs_param = *lhs_param_ptr;
			const TemplateParameterNode& rhs_param = *rhs_param_ptr;
			if (lhs_param.kind() != rhs_param.kind() ||
				lhs_param.is_variadic() != rhs_param.is_variadic() ||
				lhs_param.has_concept_constraint() != rhs_param.has_concept_constraint()) {
				return false;
			}
			if (lhs_param.kind() == TemplateParameterKind::Template &&
				!self(self, lhs_param.nested_parameters(), rhs_param.nested_parameters())) {
				return false;
			}
			if (lhs_param.kind() == TemplateParameterKind::NonType) {
				if (lhs_param.has_type() != rhs_param.has_type()) {
					return false;
				}
				if (lhs_param.has_type()) {
					if (!sameTypeSpecifierShape(
								   lhs_param.type_specifier_node(),
								   rhs_param.type_specifier_node())) {
						return false;
					}
				}
			}
		}
		return true;
	};
	return same_shape(same_shape, lhs, rhs);
}

static bool functionDeclarationsHaveMatchingShape(
	const FunctionDeclarationNode& lhs,
	const FunctionDeclarationNode& rhs) {
	if (lhs.parameter_nodes().size() != rhs.parameter_nodes().size() ||
		lhs.is_variadic() != rhs.is_variadic() ||
		lhs.is_const_member_function() != rhs.is_const_member_function() ||
		lhs.is_volatile_member_function() != rhs.is_volatile_member_function() ||
		lhs.is_noexcept() != rhs.is_noexcept() ||
		lhs.calling_convention() != rhs.calling_convention() ||
		lhs.linkage() != rhs.linkage()) {
		return false;
	}
	if (!sameTypeSpecifierShape(
			lhs.decl_node().type_specifier_node(),
			rhs.decl_node().type_specifier_node())) {
		return false;
	}
	for (size_t i = 0; i < lhs.parameter_nodes().size(); ++i) {
		if (!lhs.parameter_nodes()[i].is<DeclarationNode>() ||
			!rhs.parameter_nodes()[i].is<DeclarationNode>()) {
			return false;
		}
		const DeclarationNode& lhs_param = lhs.parameter_nodes()[i].as<DeclarationNode>();
		const DeclarationNode& rhs_param = rhs.parameter_nodes()[i].as<DeclarationNode>();
		if (lhs_param.is_parameter_pack() != rhs_param.is_parameter_pack() ||
			!sameTypeSpecifierShape(
				lhs_param.type_specifier_node(),
				rhs_param.type_specifier_node())) {
			return false;
		}
	}
	return true;
}

static bool hasLaterUsableTemplateDefinitionWithMatchingShape(
	std::span<const ASTNode> overloads,
	size_t current_index) {
	if (current_index >= overloads.size() ||
		!overloads[current_index].is<TemplateFunctionDeclarationNode>()) {
		return false;
	}
	const TemplateFunctionDeclarationNode& current_template =
		overloads[current_index].as<TemplateFunctionDeclarationNode>();
	const FunctionDeclarationNode& current_decl = current_template.function_decl_node();
	for (size_t candidate_index = current_index + 1; candidate_index < overloads.size(); ++candidate_index) {
		if (!overloads[candidate_index].is<TemplateFunctionDeclarationNode>()) {
			continue;
		}
		const TemplateFunctionDeclarationNode& candidate_template =
			overloads[candidate_index].as<TemplateFunctionDeclarationNode>();
		const FunctionDeclarationNode& candidate_decl = candidate_template.function_decl_node();
		if (!hasTemplateFunctionBodyDefinition(candidate_decl)) {
			continue;
		}
		if (!templateParameterListsHaveMatchingShape(
				current_template.template_parameters(),
				candidate_template.template_parameters())) {
			continue;
		}
		if (functionDeclarationsHaveMatchingShape(current_decl, candidate_decl)) {
			return true;
		}
	}
	return false;
}

// Compute a structural specificity score for a function template overload.
// A higher score means the overload's parameter types are more constrained/specific.
// Parameters whose type token is a bare template param (e.g. Type in swap(Type&,Type&))
// score 0 for that parameter; concrete/instantiated types (e.g. pair<F,S>) score higher.
// This is used in SFINAE overload selection to prefer the most-specialized overload.
static int computeTemplateFunctionSpecificity(const TemplateFunctionDeclarationNode& template_func) {
	// Build set of template parameter name handles for quick lookup.
	std::unordered_set<StringHandle, StringHandleHash> param_name_handles;
	for (const auto& tp : template_func.template_parameters()) {
		param_name_handles.insert(tp.nameHandle());
	}

	int score = 0;
	for (const auto& p : template_func.function_decl_node().parameter_nodes()) {
		if (!p.is<DeclarationNode>()) continue;
		const TypeSpecifierNode& ts = p.as<DeclarationNode>().type_specifier_node();

		// Check whether this param's type token matches a template parameter name.
		// If it does, it's a bare template param (low specificity).
		StringHandle tok_handle = ts.token().handle();
		bool is_bare_template_param = tok_handle.isValid() && param_name_handles.count(tok_handle) > 0;

		if (is_struct_type(ts.category()) || ts.category() == TypeCategory::UserDefined) {
			if (!is_bare_template_param) {
				// Named concrete type (e.g., pair<F,S>) — significantly more specific.
				if (const TypeInfo* ti = tryGetTypeInfo(ts.type_index())) {
					if (ti->isTemplateInstantiation()) {
						score += 2 + static_cast<int>(ti->templateArgs().size());
					} else {
						score += 2; // concrete named non-template-instantiation struct
					}
				} else {
					score += 2; // named type without TypeInfo (e.g., dependent instantiation)
				}
			}
			// bare template param → contributes 0 here
		} else if (ts.category() != TypeCategory::Invalid) {
			// Concrete built-in type → 1
			score += 1;
		}

		score += static_cast<int>(ts.pointer_depth());
		if (ts.is_lvalue_reference()) score += 1;
		if (ts.is_rvalue_reference()) score += 1;
		if (ts.is_const()) score += 1;
	}
	return score;
}

bool Parser::tryAppendDefaultTemplateArg(
	const TemplateParameterNode& param,
	std::span<const TemplateParameterNode> template_params,
	InlineVector<TemplateTypeArg, 4>& template_args,
	NamespaceHandle source_namespace) {
	FLASH_LOG_FORMAT(Templates, Debug,
					 "tryAppendDefaultTemplateArg: param='{}', has_default={}, has_default_pos={}, source_ns={}",
					 param.name(), param.has_default(), param.has_default_value_position(),
					 source_namespace.isValid() ? gNamespaceRegistry.getQualifiedName(source_namespace) : "(none)");
	if (!param.has_default()) {
		return false;
	}

	const ASTNode& default_node = param.default_value();
	const TemplateSubstitutionFailurePolicy failure_policy = currentTemplateSubstitutionFailurePolicy();
	TemplateEnvironment default_arg_environment = buildTemplateEnvironment(
		template_params,
		std::span<const TemplateTypeArg>(template_args.data(), template_args.size()),
		nullptr);

	auto appendEvaluatedNonTypeArg = [&](const ASTNode& expr) -> bool {
		ConstExpr::EvaluationContext eval_ctx(gSymbolTable, *this);
		eval_ctx.template_environment = default_arg_environment;
		auto eval_sub_map = buildSubstitutionParamMap(default_arg_environment);
		eval_ctx.template_param_names.assign(
			eval_sub_map.param_order.begin(),
			eval_sub_map.param_order.end());
		eval_ctx.template_args.reserve(eval_sub_map.param_order.size());
		for (std::string_view param_name : eval_sub_map.param_order) {
			auto arg_it = eval_sub_map.param_map.find(param_name);
			if (arg_it != eval_sub_map.param_map.end()) {
				eval_ctx.template_args.push_back(arg_it->second);
			}
		}
		auto eval_result = ConstExpr::Evaluator::evaluate(expr, eval_ctx);
		if (!eval_result.success()) {
			if (eval_result.error_type == ConstExpr::EvalErrorType::TemplateDependentExpression &&
				failure_policy == TemplateSubstitutionFailurePolicy::ShapeOnly) {
				if (!param.has_type()) {
					throw InternalError("Non-type template parameter with a dependent default value must declare an explicit type");
				}
				TemplateTypeArg dependent_default = TemplateTypeArg::makeDependentValue(
					param.nameHandle(),
					param.type_specifier_node().type());
				dependent_default.dependent_expr = expr;
				template_args.push_back(std::move(dependent_default));
				return true;
			}
			return false;
		}
		if (param.has_type()) {
			ASTNode substituted_type_node = substituteTemplateParameters(
				ASTNode::emplace_node<TypeSpecifierNode>(param.type_specifier_node()),
				template_params,
				std::span<const TemplateTypeArg>(template_args.data(), template_args.size()));
			if (substituted_type_node.is<TypeSpecifierNode>()) {
				template_args.push_back(templateTypeArgFromEvalResult(
					eval_result,
					substituted_type_node.as<TypeSpecifierNode>()));
			} else {
				template_args.push_back(templateTypeArgFromEvalResult(eval_result));
			}
		} else {
			template_args.push_back(templateTypeArgFromEvalResult(eval_result));
		}
		default_arg_environment = buildTemplateEnvironment(
			template_params,
			std::span<const TemplateTypeArg>(template_args.data(), template_args.size()),
			nullptr);
		return true;
	};

	// Helper to enter the source namespace for reparsing if provided
	// Pushes ALL ancestor namespace scopes (outermost first) so that unqualified
	// lookup correctly walks from the declaration namespace up to global.
	// Returns the number of scopes pushed (to be passed to exitSourceNamespaceIfNeeded).
	auto enterSourceNamespaceIfNeeded = [&]() -> int {
		if (!source_namespace.isValid() || source_namespace.isGlobal()) {
			return 0;
		}
		// Collect chain from innermost to outermost (excluding global)
		InlineVector<NamespaceHandle, 8> chain;
		NamespaceHandle cur = source_namespace;
		while (cur.isValid() && !cur.isGlobal()) {
			chain.push_back(cur);
			cur = gNamespaceRegistry.getParent(cur);
		}
		// Push from outermost to innermost so lookup finds ancestors first
		for (int i = static_cast<int>(chain.size()) - 1; i >= 0; --i) {
			gSymbolTable.enter_namespace(chain[i]);
		}
		return static_cast<int>(chain.size());
	};
	auto exitSourceNamespaceIfNeeded = [&](int entered) {
		for (int i = 0; i < entered; ++i) {
			gSymbolTable.exit_scope();
		}
	};

	auto tryReparseNonTypeDefaultArg = [&]() -> bool {
		if (!param.has_default_value_position() || template_args.empty()) {
			return false;
		}

		FlashCpp::ScopedState guard_ptb(parsing_template_depth_);
		FlashCpp::ScopedState guard_param_names(currentTemplateParamState());
		FlashCpp::ScopedState guard_subs(template_param_substitutions_);
		FlashCpp::ScopedState guard_sfinae_map(sfinae_type_map_);
		ScopedParserInstantiationContext guard_instantiation_mode(*this, TemplateInstantiationMode::SoftProbe, StringHandle{});
		parsing_template_depth_ = 0;
		clearCurrentTemplateParameters();
		template_param_substitutions_.clear();
		populateTemplateParamSubstitutions(template_param_substitutions_, default_arg_environment);
		sfinae_type_map_.clear();

		SaveHandle sfinae_pos = save_token_position();
		restore_lexer_position_only(param.default_value_position());

		FlashCpp::TemplateParameterScope sfinae_scope;
		registerTypeParamsInScope(template_params, template_args, sfinae_scope, &sfinae_type_map_);

		int entered_ns = enterSourceNamespaceIfNeeded();
		auto reparse_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::TemplateTypeArg);
		exitSourceNamespaceIfNeeded(entered_ns);
		restore_lexer_position_only(sfinae_pos);

		if (reparse_result.is_error() || !reparse_result.node().has_value() ||
			!reparse_result.node()->is<ExpressionNode>()) {
			FLASH_LOG_FORMAT(Templates, Debug,
							 "SFINAE: non-type default template arg re-parse failed for param '{}', rejecting overload",
							 param.name());
			return false;
		}

		return appendEvaluatedNonTypeArg(*reparse_result.node());
	};
	if (param.kind() == TemplateParameterKind::Type) {
		if (param.has_default_value_position() && !template_args.empty()) {
			FlashCpp::ScopedState guard_ptb(parsing_template_depth_);
			FlashCpp::ScopedState guard_param_names(currentTemplateParamState());
			FlashCpp::ScopedState guard_subs(template_param_substitutions_);
			FlashCpp::ScopedState guard_sfinae_map(sfinae_type_map_);
			ScopedParserInstantiationContext guard_instantiation_mode(*this, TemplateInstantiationMode::SoftProbe, StringHandle{});
			parsing_template_depth_ = 0;
			clearCurrentTemplateParameters();
			template_param_substitutions_.clear();
			populateTemplateParamSubstitutions(template_param_substitutions_, default_arg_environment);
			sfinae_type_map_.clear();

			SaveHandle sfinae_pos = save_token_position();
			restore_lexer_position_only(param.default_value_position());

			FlashCpp::TemplateParameterScope sfinae_scope;
			registerTypeParamsInScope(template_params, template_args, sfinae_scope, &sfinae_type_map_);

			int entered_ns = enterSourceNamespaceIfNeeded();
			FLASH_LOG_FORMAT(Templates, Debug, "SFINAE reparse: entered_ns={}, current_ns={}",
				entered_ns,
				gSymbolTable.get_current_namespace_handle().isValid()
					? gNamespaceRegistry.getQualifiedName(gSymbolTable.get_current_namespace_handle())
					: "(global)");
			auto reparse_result = parse_type_specifier();
			exitSourceNamespaceIfNeeded(entered_ns);
			restore_lexer_position_only(sfinae_pos);

			if (reparse_result.is_error() || !reparse_result.node().has_value() ||
				!reparse_result.node()->is<TypeSpecifierNode>()) {
				FLASH_LOG_FORMAT(Templates, Debug,
								 "SFINAE: default template arg re-parse failed for param '{}', rejecting overload",
								 param.name());
				return false;
			}
			const TypeSpecifierNode& reparsed_type = reparse_result.node()->as<TypeSpecifierNode>();
			TemplateTypeArg default_arg(reparsed_type);
			if (is_builtin_type(default_arg.typeEnum())) {
				default_arg.type_index = nativeTypeIndex(default_arg.typeEnum());
			}
			template_args.push_back(default_arg);
			return true;
		}

		if (!default_node.is<TypeSpecifierNode>()) {
			return false;
		}

		const TypeSpecifierNode& default_type = default_node.as<TypeSpecifierNode>();
		TemplateTypeArg default_arg(default_type);
		if (is_builtin_type(default_arg.typeEnum())) {
			default_arg.type_index = nativeTypeIndex(default_arg.typeEnum());
		}
		template_args.push_back(default_arg);
		return true;
	}

	if (param.kind() == TemplateParameterKind::NonType && default_node.is<ExpressionNode>()) {
		InlineVector<TemplateParameterNode, 4> params_vec(template_params);
		ASTNode substituted_default = substituteNonTypeDefaultExpression(
			default_node, params_vec,
			std::span<const TemplateTypeArg>(template_args.data(), template_args.size()));
		if (appendEvaluatedNonTypeArg(substituted_default)) {
			return true;
		}

		if (tryReparseNonTypeDefaultArg()) {
			return true;
		}

		if (defaultExpressionReferencesTemplateParams(default_node, template_params)) {
			if (failure_policy == TemplateSubstitutionFailurePolicy::ShapeOnly) {
				if (!param.has_type()) {
					throw InternalError("ShapeOnly non-type template parameter default requires declared type");
				}
				TemplateTypeArg dependent_default = TemplateTypeArg::makeDependentValue(
					param.nameHandle(),
					param.type_specifier_node().type());
				dependent_default.dependent_expr = default_node;
				template_args.push_back(std::move(dependent_default));
				default_arg_environment = buildTemplateEnvironment(
					template_params,
					std::span<const TemplateTypeArg>(template_args.data(), template_args.size()),
					nullptr);
				return true;
			}
			return false;
		}

		return appendEvaluatedNonTypeArg(default_node);
	}

	if (param.kind() == TemplateParameterKind::Template && default_node.is<TypeSpecifierNode>()) {
		const TypeSpecifierNode& default_type = default_node.as<TypeSpecifierNode>();
		StringHandle tpl_name_handle = StringTable::getOrInternStringHandle(default_type.token().value());
		if (const TypeInfo* type_info = tryGetTypeInfo(default_type.type_index())) {
			tpl_name_handle = type_info->name();
		}
		template_args.push_back(TemplateTypeArg::makeTemplate(tpl_name_handle));
		return true;
	}

	return false;
}

template <typename ParamContainer, typename ArgContainer>
static void applyTemplateArgIndirection(
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
			// ArgContainer can be either std::vector<TemplateTypeArg> (which stores full
			// array_dimensions) or InlineVector<TypeInfo::TemplateArgInfo> (single optional
			// array_size field).  Both remain live in the codebase so both branches are
			// permanent, not transitional.
			if constexpr (requires(decltype(*arg) a) { a.array_size(); }) {
				substituted_type.set_array_dimensions(arg->array_dimensions);
			} else {
				substituted_type.set_array(true, arg->array_size);
			}
		}
	}
}

// Helper: register type-kind template parameters as TypeInfo / getTypesByNameMap() entries so
// that body re-parsing can resolve their names.  Non-type (value) parameters are
// intentionally skipped: makeValue() leaves base_type as the value-type (e.g. Type::Int
// for int N), so registering it as a TypeInfo entry would erroneously add a type named
// "N" to getTypesByNameMap() and confuse subsequent type lookups.
// Template-template parameters are also skipped for the same reason.
//
// preserve_ref_qualifier: pass true for paths where the TemplateTypeArg ref_qualifier was
//   set from user-written explicit args or class-template instantiation args (e.g., T
//   bound to int& from a class<int&> instantiation).  Pass false for deduction paths
//   where lvalue-ness of the call-site argument must NOT propagate to the TypeInfo entry.
void registerTypeParamsInScope(
	const InlineVector<StringHandle, 4>& param_names,
	const InlineVector<TemplateTypeArg, 4>& type_args,
	FlashCpp::TemplateParameterScope& scope,
	bool preserve_ref_qualifier) {
	for (size_t i = 0; i < param_names.size() && i < type_args.size(); ++i) {
		const TemplateTypeArg& arg = type_args[i];
		if (arg.is_value)
			continue;  // Non-type (value) params must NOT be registered as TypeInfo
		if (arg.is_template_template_arg)
			continue;  // Template-template params don't represent concrete types
		auto& type_info = registerTemplateTypeBinding(param_names[i], arg);
		applyRegisteredTypeBindingMetadata(type_info, arg, preserve_ref_qualifier);
		scope.addParameter(&type_info);
	}
}

namespace {

template <typename RegisterFn>
void forEachEnvironmentBinding(
	const TemplateEnvironment& environment,
	RegisterFn&& register_fn) {
	if (environment.parent != nullptr) {
		forEachEnvironmentBinding(*environment.parent, register_fn);
	}
	for (const TemplateBinding& binding : environment.bindings) {
		register_fn(binding);
	}
}

} // namespace

void registerTypeParamsInScope(
	const TemplateEnvironment& environment,
	FlashCpp::TemplateParameterScope& scope,
	bool preserve_ref_qualifier) {
	forEachEnvironmentBinding(
		environment,
		[&](const TemplateBinding& binding) {
			if (binding.is_pack || binding.args.empty()) {
				return;
			}
			const TemplateTypeArg& arg = binding.args.front();
			if (arg.is_value || arg.is_template_template_arg) {
				return;
			}
			auto& type_info = registerTemplateTypeBinding(binding.name, arg);
			applyRegisteredTypeBindingMetadata(type_info, arg, preserve_ref_qualifier);
			scope.addParameter(&type_info);
		});
}

void registerTypeParamsInScope(
	const TemplateEnvironment& environment,
	FlashCpp::TemplateParameterScope& scope,
	std::unordered_map<StringHandle, TypeIndex, StringHash, StringEqual>* sfinae_map) {
	forEachEnvironmentBinding(
		environment,
		[&](const TemplateBinding& binding) {
			if (binding.is_pack || binding.args.empty()) {
				return;
			}
			const TemplateTypeArg& arg = binding.args.front();
			if (arg.is_value || arg.is_template_template_arg) {
				return;
			}
			auto& type_info = registerTemplateTypeBinding(binding.name, arg);
			applyRegisteredTypeBindingMetadata(type_info, arg, true);
			scope.addParameter(&type_info);
			if (sfinae_map) {
				(*sfinae_map)[type_info.name()] = arg.type_index;
			}
		});
}

// ─────────────────────────────────────────────────────────────────────────────
// registerTypeParamsInScope — ASTNode-based overload for SFINAE trailing-return
// type re-parse.  Takes the raw template_param_nodes ASTNode vector (handles
// mixed TemplateParameterNode / non-TemplateParameterNode safely), plus an
// optional sfinae_map to populate alongside getTypesByNameMap().  Unlike the
// string_view overloads above, it does not need a pre-built param_names vector,
// so the caller avoids index-alignment issues.
// ─────────────────────────────────────────────────────────────────────────────
void registerTypeParamsInScope(
	const InlineVector<TemplateParameterNode, 4>& template_param_nodes,
	std::span<const TemplateTypeArg> template_args,
	FlashCpp::TemplateParameterScope& scope,
	bool preserve_ref_qualifier) {
	forEachNonPackTemplateParamArgBinding(
		template_param_nodes,
		template_args,
		[&](const TemplateParameterNode& param, const TemplateTypeArg& arg, size_t) {
			if (arg.is_value || arg.is_template_template_arg)
				return;
			auto& type_info = registerTemplateTypeBinding(param.nameHandle(), arg);
			applyRegisteredTypeBindingMetadata(type_info, arg, preserve_ref_qualifier);
			scope.addParameter(&type_info);
		});
}

void registerTypeParamsInScope(
	const InlineVector<ASTNode, 4>& template_param_nodes,
	std::span<const TemplateTypeArg> template_args,
	FlashCpp::TemplateParameterScope& scope,
	bool preserve_ref_qualifier) {
	forEachNonPackTemplateParamArgBinding(
		template_param_nodes,
		template_args,
		[&](const TemplateParameterNode& param, const TemplateTypeArg& arg, size_t) {
			if (arg.is_value || arg.is_template_template_arg)
				return;
			auto& type_info = registerTemplateTypeBinding(param.nameHandle(), arg);
			applyRegisteredTypeBindingMetadata(type_info, arg, preserve_ref_qualifier);
			scope.addParameter(&type_info);
		});
}

void registerTypeParamsInScope(
	const InlineVector<TemplateParameterNode, 4>& template_param_nodes,
	std::span<const TemplateTypeArg> template_args,
	FlashCpp::TemplateParameterScope& scope,
	std::unordered_map<StringHandle, TypeIndex, StringHash, StringEqual>* sfinae_map) {
	forEachNonPackTemplateParamArgBinding(
		template_param_nodes,
		template_args,
		[&](const TemplateParameterNode& param, const TemplateTypeArg& arg, size_t) {
			if (arg.is_value || arg.is_template_template_arg)
				return;
			auto& type_info = registerTemplateTypeBinding(param.nameHandle(), arg);
			applyRegisteredTypeBindingMetadata(type_info, arg, true);
			scope.addParameter(&type_info);
			if (sfinae_map)
				(*sfinae_map)[type_info.name()] = arg.type_index;
		});
}

void registerTypeParamsInScope(
	const InlineVector<ASTNode, 4>& template_param_nodes,
	std::span<const TemplateTypeArg> template_args,
	FlashCpp::TemplateParameterScope& scope,
	std::unordered_map<StringHandle, TypeIndex, StringHash, StringEqual>* sfinae_map) {
	forEachNonPackTemplateParamArgBinding(
		template_param_nodes,
		template_args,
		[&](const TemplateParameterNode& param, const TemplateTypeArg& arg, size_t) {
			if (arg.is_value || arg.is_template_template_arg)
				return;
			auto& type_info = registerTemplateTypeBinding(param.nameHandle(), arg);
			applyRegisteredTypeBindingMetadata(type_info, arg, true);
			scope.addParameter(&type_info);
			if (sfinae_map)
				(*sfinae_map)[type_info.name()] = arg.type_index;
		});
}

void registerTypeParamsInScope(
	std::span<const TemplateParameterNode> template_param_nodes,
	std::span<const TemplateTypeArg> template_args,
	FlashCpp::TemplateParameterScope& scope,
	bool preserve_ref_qualifier) {
	forEachNonPackTemplateParamArgBinding(
		template_param_nodes,
		template_args,
		[&](const TemplateParameterNode& param, const TemplateTypeArg& arg, size_t) {
			if (arg.is_value || arg.is_template_template_arg)
				return;
			auto& type_info = registerTemplateTypeBinding(param.nameHandle(), arg);
			applyRegisteredTypeBindingMetadata(type_info, arg, preserve_ref_qualifier);
			scope.addParameter(&type_info);
		});
}

void registerTypeParamsInScope(
	std::span<const TemplateParameterNode> template_param_nodes,
	std::span<const TemplateTypeArg> template_args,
	FlashCpp::TemplateParameterScope& scope,
	std::unordered_map<StringHandle, TypeIndex, StringHash, StringEqual>* sfinae_map) {
	forEachNonPackTemplateParamArgBinding(
		template_param_nodes,
		template_args,
		[&](const TemplateParameterNode& param, const TemplateTypeArg& arg, size_t) {
			if (arg.is_value || arg.is_template_template_arg)
				return;
			auto& type_info = registerTemplateTypeBinding(param.nameHandle(), arg);
			applyRegisteredTypeBindingMetadata(type_info, arg, true);
			scope.addParameter(&type_info);
			if (sfinae_map)
				(*sfinae_map)[type_info.name()] = arg.type_index;
		});
}

// ─────────────────────────────────────────────────────────────────────────────
// registerOuterBindingInScope
// ─────────────────────────────────────────────────────────────────────────────
// Register the outer-class template parameter bindings (e.g., T→int carried by
// OuterTemplateBinding) into getTypesByNameMap() and the given TemplateParameterScope.
// Optionally also populates sfinae_map for the SFINAE trailing-return path.
// Called from both the body-reparse and SFINAE paths of
// try_instantiate_member_function_template_explicit / _core.
// ─────────────────────────────────────────────────────────────────────────────
void registerOuterBindingInScope(
	const OuterTemplateBinding& outer_binding,
	FlashCpp::TemplateParameterScope& scope,
	std::unordered_map<StringHandle, TypeIndex, StringHash, StringEqual>* sfinae_map) {
	auto register_type_binding = [&](StringHandle param_name, const TemplateTypeArg& arg, uint32_t size) -> TypeInfo& {
		if (arg.type_index.is_valid()) {
			return add_type_alias_copy(
				param_name,
				arg.type_index.withCategory(arg.typeEnum()),
				size);
		}
		return add_template_param_type(param_name, arg.typeEnum(), size);
	};
	for (size_t i = 0; i < outer_binding.param_names.size() && i < outer_binding.param_args.size(); ++i) {
		const TemplateTypeArg& arg = outer_binding.param_args[i];
		uint32_t size = get_type_size_bits(arg.typeEnum());
		if (const TypeInfo* arg_type_info = tryGetTypeInfo(arg.type_index))
			size = static_cast<uint32_t>(arg_type_info->sizeInBits().value);
		auto& type_info = register_type_binding(
			outer_binding.param_names[i], arg, size);
		scope.addParameter(&type_info);
		if (sfinae_map)
			(*sfinae_map)[type_info.name()] = arg.type_index;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// populateTemplateParamSubstitutions
// ─────────────────────────────────────────────────────────────────────────────
// Populate a TemplateParamSubstitution vector from a normalized template
// environment for body-reparse paths. Non-type and template-template entries
// are converted to value/type/template-template substitutions so parse_block()
// can resolve references like "return N;" without touching getTypesByNameMap().
// ─────────────────────────────────────────────────────────────────────────────
void Parser::populateTemplateParamSubstitutions(
	InlineVector<TemplateParamSubstitution, 4>& subs,
	const TemplateEnvironment& environment) {
	auto make_substitution = [](StringHandle param_name, const TemplateTypeArg& arg) {
		TemplateParamSubstitution subst;
		subst.param_name = param_name;
		if (arg.is_template_template_arg) {
			subst.is_template_template_param = true;
			subst.concrete_template_name = arg.template_name_handle;
			return subst;
		}
		if (arg.is_value) {
			subst.is_value_param = true;
			subst.value = arg.value;
			subst.value_type = arg.typeEnum();
			if (arg.has_typed_value_identity)
				subst.typed_value_identity = arg.typed_value_identity;
			return subst;
		}
		subst.is_type_param = true;
		subst.substituted_type = arg;
		return subst;
	};
	forEachEnvironmentBinding(
		environment,
		[&](const TemplateBinding& binding) {
			if (binding.is_pack || binding.args.empty()) {
				return;
			}
			TemplateTypeArg arg = binding.args.front();
			if (binding.kind == TemplateParameterKind::Template) {
				arg.is_template_template_arg = true;
				if (!arg.template_name_handle.isValid()) {
					if (arg.type_index.is_valid()) {
						if (const TypeInfo* ti = tryGetTypeInfo(arg.type_index)) {
							arg.template_name_handle = ti->name_;
						}
					}
					if (!arg.template_name_handle.isValid() && arg.dependent_name.isValid()) {
						arg.template_name_handle = arg.dependent_name;
					}
				}
			}
			subs.push_back(make_substitution(binding.name, arg));
		});
}
// ─────────────────────────────────────────────────────────────────────────────
// Shared helper: re-parse a template function body with concrete argument
// substitution and set the result as new_func_ref's definition.
//
// Called by both try_instantiate_template_explicit (preserve_ref_qualifier=true)
// and try_instantiate_single_template (preserve_ref_qualifier=false) *after*
// cycle detection has passed.  Pack-parameter state management and cycle
// detection remain the responsibility of the callers because they differ
// between the two paths.
void Parser::reparse_template_function_body(
	FunctionDeclarationNode& new_func_ref,
	const FunctionDeclarationNode& func_decl,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args,
	bool preserve_ref_qualifier) {
	// Depth guard: function-template body replay can recursively re-enter via
	// expressions inside the body that instantiate further templates.  libstdc++
	// headers like <string_view>, <vector>, and <iterator> reach dozens of nested
	// replay frames through iterator_traits / __normal_iterator SFINAE chains,
	// and each frame carries substantial parser state, quickly exhausting the
	// thread's 16MB stack.  Bail out cleanly before we hit the guard page so the
	// caller sees an error instead of a SIGSEGV.
	static thread_local size_t s_body_replay_depth = 0;
	static thread_local bool s_body_replay_depth_warned = false;
	static constexpr size_t MAX_BODY_REPLAY_DEPTH = 24;
	++s_body_replay_depth;
	struct DepthGuard {
		~DepthGuard() {
			if (--s_body_replay_depth == 0) {
				s_body_replay_depth_warned = false;
			}
		}
	} depth_guard;
	if (s_body_replay_depth > MAX_BODY_REPLAY_DEPTH) {
		if (!s_body_replay_depth_warned) {
			FLASH_LOG(Templates, Error, "Max template function body replay depth (", MAX_BODY_REPLAY_DEPTH,
					  ") exceeded. Possible recursive template instantiation in function body.");
			s_body_replay_depth_warned = true;
		}
		return;
	}
	// pack_param_info_ must be set up by the caller before entering here.
	// Both callers (try_instantiate_template_explicit and try_instantiate_single_template)
	// correctly compute it from the expanded parameter list and own the save/restore
	// lifecycle around this call.  We do not touch pack_param_info_ here to avoid
	// duplicating that logic with a version that is broken for complex pack types such as
	// std::pair<Args,int>... (where type_name != tparam.name()).

	// Collect parameter names and register TypeInfo entries for type params.
	FlashCpp::TemplateParameterScope template_scope;
	InlineVector<StringHandle, 4> param_names;
	param_names.reserve(template_params.size());
	for (const TemplateParameterNode& template_param : template_params) {
		param_names.push_back(template_param.nameHandle());
	}
	// preserve_ref_qualifier=true for the explicit path (user-written T=int& must be
	// reflected in TypeInfo); false for the deduced path.
	InlineVector<TemplateParameterNode, 4> template_param_nodes;
	template_param_nodes.reserve(template_params.size());
	for (const TemplateParameterNode& template_param_node : template_params) {
		template_param_nodes.push_back(template_param_node);
	}
	registerTypeParamsInScope(template_param_nodes, template_args, template_scope, preserve_ref_qualifier);

	// Save lexer position and function context.
	SaveHandle current_pos = save_token_position();
	FlashCpp::ScopedState guard_current_function(current_function_);

	// Restore lexer to the template body start.
	restore_lexer_position_only(func_decl.template_body_position());

	auto enterSourceNamespaceIfNeeded = [&]() -> int {
		NamespaceHandle source_namespace = func_decl.namespace_handle();
		if (!source_namespace.isValid() || source_namespace.isGlobal()) {
			return 0;
		}
		InlineVector<NamespaceHandle, 8> chain;
		NamespaceHandle current = source_namespace;
		while (current.isValid() && !current.isGlobal()) {
			chain.push_back(current);
			current = gNamespaceRegistry.getParent(current);
		}
		for (int i = static_cast<int>(chain.size()) - 1; i >= 0; --i) {
			gSymbolTable.enter_namespace(chain[i]);
		}
		return static_cast<int>(chain.size());
	};
	auto exitSourceNamespaceIfNeeded = [&](int entered) {
		for (int i = 0; i < entered; ++i) {
			gSymbolTable.exit_scope();
		}
	};
	const int entered_namespace_count = enterSourceNamespaceIfNeeded();
	auto exit_source_namespace = ScopeGuard([&]() {
		exitSourceNamespaceIfNeeded(entered_namespace_count);
	});

	// Enter function scope, set current function, register parameters.
	gSymbolTable.enter_scope(ScopeType::Function);
	current_function_ = &new_func_ref;
	for (const auto& param : new_func_ref.parameter_nodes()) {
		if (param.is<DeclarationNode>()) {
			gSymbolTable.insert(param.as<DeclarationNode>().identifier_token().value(), param);
		}
	}

	// Populate template_param_substitutions_ so the body parser can resolve
	// non-type params (e.g., "return N;") and type params used in variable
	// template instantiations inside the body.
	{
		FlashCpp::ScopedState guard_subs(template_param_substitutions_);
		TemplateInstantiationContext substitution_context = buildTemplateInstantiationContext(
			template_params,
			template_args,
			nullptr,
			currentTemplateSubstitutionFailurePolicy());
		TemplateEnvironment& substitution_environment = substitution_context.environment;
		populateTemplateParamSubstitutions(template_param_substitutions_, substitution_environment);

		// Phase 1 (C++20 [temp.res]/9): record the template body's opening-brace line so
		// createBoundIdentifier can detect names that were not visible at definition time.
		{
			SaveHandle body_position = func_decl.template_body_position();
			if (body_position < saved_tokens_.size() && saved_tokens_[body_position].has_value()) {
				const SavedToken& saved_token = *saved_tokens_[body_position];
				phase1_cutoff_line_ = saved_token.current_token_.line();
				phase1_cutoff_file_idx_ = saved_token.current_token_.file_index();
				phase1_violation_token_.reset();
			}
		}
		TemplateDefinitionLookupContext definition_lookup_context;
		definition_lookup_context.definition_line = phase1_cutoff_line_;
		definition_lookup_context.definition_file_index = phase1_cutoff_file_idx_;
		definition_lookup_context.definition_namespace = gSymbolTable.get_current_namespace_handle();
		if (current_function_ != nullptr && !current_function_->parent_struct_name().empty()) {
			definition_lookup_context.current_instantiation_name =
				StringTable::getOrInternStringHandle(current_function_->parent_struct_name());
		}
		substitution_context.definition_lookup_context = definition_lookup_context.is_valid()
			? &definition_lookup_context
			: nullptr;
		ScopedDefinitionLookupContext ctx_scope(
			current_template_definition_lookup_context_,
			substitution_context.definition_lookup_context);

		// Parse the body, substitute template parameters, then install as definition.
		{
			FlashCpp::ScopedState guard_param_names(currentTemplateParamState());
			for (const auto& pn : param_names) {
				pushCurrentTemplateParamName(pn);
			}

			auto block_result = parse_function_body();  // handles function-try-blocks
			if (!block_result.is_error() && block_result.node().has_value()) {
				new_func_ref.set_definition(
					substituteTemplateParameters(*block_result.node(), substitution_context));
			}
		}  // current_template_param_names_ restored here by ScopedState
	} // template_param_substitutions_ restored here by ScopedState

	// Clean up scope.
	current_function_ = nullptr;
	gSymbolTable.exit_scope();
	restore_lexer_position_only(current_pos);
	discard_saved_token(current_pos);

	// Check for Phase 1 violations (after full cleanup so RAII guards are still in scope).
	phase1_cutoff_line_ = 0;
	phase1_cutoff_file_idx_ = SIZE_MAX;
	if (phase1_violation_token_.has_value()) {
		auto tok = *phase1_violation_token_;
		phase1_violation_token_.reset();
		throw CompileError(
			std::string("non-dependent name '").append(tok.value()).append("' was not declared before the template definition (C++20 [temp.res]/9)"));
	}
	// template_scope RAII guard removes TypeInfo entries automatically.
}

namespace {
void collectDependentTemplateParamNamesFromTemplateArgRecursive(
	const TypeInfo::TemplateArgInfo& pattern_arg,
	const std::unordered_map<StringHandle, const TemplateParameterNode*, StringHash, StringEqual>&
		tparam_nodes_by_name,
	StringHandle& primary_name,
	std::unordered_set<StringHandle, StringHash, StringEqual>& dependent_param_names) {
	if (pattern_arg.dependent_name.isValid()) {
		auto param_it = tparam_nodes_by_name.find(pattern_arg.dependent_name);
		if (param_it != tparam_nodes_by_name.end()) {
			dependent_param_names.insert(pattern_arg.dependent_name);
			if (!primary_name.isValid()) {
				primary_name = pattern_arg.dependent_name;
			}
		}
	}

	const TypeInfo* nested_pattern_info = tryGetTypeInfo(pattern_arg.type_index);
	if (nested_pattern_info == nullptr || !nested_pattern_info->isTemplateInstantiation()) {
		return;
	}

	for (const auto& nested_arg : nested_pattern_info->templateArgs()) {
		collectDependentTemplateParamNamesFromTemplateArgRecursive(
			nested_arg,
			tparam_nodes_by_name,
			primary_name,
			dependent_param_names);
	}
}

std::optional<TemplateTypeArg> extractNestedTemplateArgFromTemplateArgRecursive(
	const TypeInfo::TemplateArgInfo& pattern_arg,
	const TypeInfo::TemplateArgInfo& concrete_arg,
	StringHandle dependent_name) {
	if (pattern_arg.dependent_name == dependent_name) {
		if (concrete_arg.is_value) {
			return TemplateTypeArg::makeValue(concrete_arg.intValue(), concrete_arg.typeEnum());
		}
		return TemplateTypeArg::makeTypeSpecifier(*makeTypeSpecifierFromTemplateArgInfo(concrete_arg, Token()));
	}

	const TypeInfo* nested_pattern_info = tryGetTypeInfo(pattern_arg.type_index);
	const TypeInfo* nested_concrete_info = tryGetTypeInfo(concrete_arg.type_index);
	if (nested_pattern_info == nullptr || nested_concrete_info == nullptr ||
		!nested_pattern_info->isTemplateInstantiation() ||
		!nested_concrete_info->isTemplateInstantiation() ||
		nested_pattern_info->baseTemplateName() != nested_concrete_info->baseTemplateName()) {
		return std::nullopt;
	}

	const auto& pattern_args = nested_pattern_info->templateArgs();
	const auto& concrete_args = nested_concrete_info->templateArgs();
	for (size_t i = 0; i < pattern_args.size() && i < concrete_args.size(); ++i) {
		auto nested_match = extractNestedTemplateArgFromTemplateArgRecursive(
			pattern_args[i],
			concrete_args[i],
			dependent_name);
		if (nested_match.has_value()) {
			return nested_match;
		}
	}

	return std::nullopt;
}

std::optional<bool> preDeduceTemplateArgsFromTemplateArgRecursive(
	const TypeInfo::TemplateArgInfo& pattern_arg,
	const TypeInfo::TemplateArgInfo& concrete_arg,
	const std::unordered_map<StringHandle, const TemplateParameterNode*, StringHash, StringEqual>&
		tparam_nodes_by_name,
	std::unordered_map<StringHandle, TemplateTypeArg, StringHash, StringEqual>& param_name_to_arg,
	int recursion_depth) {
	bool produced_deduction = false;

	if (pattern_arg.dependent_name.isValid()) {
		auto param_it = tparam_nodes_by_name.find(pattern_arg.dependent_name);
		if (param_it != tparam_nodes_by_name.end()) {
			TemplateTypeArg new_arg = concrete_arg.is_value
				? TemplateTypeArg::makeValue(concrete_arg.intValue(), concrete_arg.typeEnum())
				: TemplateTypeArg::makeTypeSpecifier(*makeTypeSpecifierFromTemplateArgInfo(concrete_arg, Token()));
			auto [existing_it, inserted] = param_name_to_arg.emplace(pattern_arg.dependent_name, new_arg);
			if (!inserted && !(existing_it->second == new_arg)) {
				FLASH_LOG_FORMAT(Templates, Error,
								 "[depth={}]: Conflicting deduction for template param '{}'",
								 recursion_depth,
								 StringTable::getStringView(pattern_arg.dependent_name));
				return std::nullopt;
			}
			produced_deduction = true;
		}
	}

	const TypeInfo* nested_pattern_info = tryGetTypeInfo(pattern_arg.type_index);
	const TypeInfo* nested_concrete_info = tryGetTypeInfo(concrete_arg.type_index);
	if (nested_pattern_info == nullptr || nested_concrete_info == nullptr ||
		!nested_pattern_info->isTemplateInstantiation() ||
		!nested_concrete_info->isTemplateInstantiation() ||
		nested_pattern_info->baseTemplateName() != nested_concrete_info->baseTemplateName()) {
		return produced_deduction;
	}

	const auto& pattern_args = nested_pattern_info->templateArgs();
	const auto& concrete_args = nested_concrete_info->templateArgs();
	for (size_t i = 0; i < pattern_args.size() && i < concrete_args.size(); ++i) {
		auto nested_result = preDeduceTemplateArgsFromTemplateArgRecursive(
			pattern_args[i],
			concrete_args[i],
			tparam_nodes_by_name,
			param_name_to_arg,
			recursion_depth);
		if (!nested_result.has_value()) {
			return std::nullopt;
		}
		produced_deduction = produced_deduction || *nested_result;
	}

	return produced_deduction;
}
}

void Parser::collectDependentTemplateParamNamesFromType(
	TypeIndex pattern_type_index,
	const std::unordered_map<StringHandle, const TemplateParameterNode*, StringHash, StringEqual>&
		tparam_nodes_by_name,
	StringHandle& primary_name,
	std::unordered_set<StringHandle, StringHash, StringEqual>& dependent_param_names) {
	const TypeInfo* pattern_info = tryGetTypeInfo(pattern_type_index);
	if (pattern_info == nullptr) {
		return;
	}
	if (!primary_name.isValid()) {
		StringHandle direct_name = pattern_info->name();
		if (tparam_nodes_by_name.count(direct_name)) {
			primary_name = direct_name;
			dependent_param_names.insert(direct_name);
		}
	}
	if (!pattern_info->isTemplateInstantiation()) {
		return;
	}
	for (const auto& pattern_arg : pattern_info->templateArgs()) {
		collectDependentTemplateParamNamesFromTemplateArgRecursive(
			pattern_arg,
			tparam_nodes_by_name,
			primary_name,
			dependent_param_names);
	}
}

std::optional<TemplateTypeArg> Parser::extractNestedTemplateArgForDependentName(
	TypeIndex pattern_type_index,
	TypeIndex concrete_type_index,
	StringHandle dependent_name) {
	const TypeInfo* pattern_info = tryGetTypeInfo(pattern_type_index);
	const TypeInfo* concrete_info = tryGetTypeInfo(concrete_type_index);
	if (pattern_info == nullptr || concrete_info == nullptr ||
		!pattern_info->isTemplateInstantiation() ||
		!concrete_info->isTemplateInstantiation() ||
		pattern_info->baseTemplateName() != concrete_info->baseTemplateName()) {
		return std::nullopt;
	}

	const auto& pattern_args = pattern_info->templateArgs();
	const auto& concrete_args = concrete_info->templateArgs();
	for (size_t i = 0; i < pattern_args.size() && i < concrete_args.size(); ++i) {
		auto nested_match = extractNestedTemplateArgFromTemplateArgRecursive(
			pattern_args[i],
			concrete_args[i],
			dependent_name);
		if (nested_match.has_value()) {
			return nested_match;
		}
	}

	return std::nullopt;
}

std::optional<bool> Parser::preDeduceTemplateArgsFromMatchingTypes(
	const TypeSpecifierNode& pattern_type,
	const TypeSpecifierNode& concrete_type,
	const std::unordered_map<StringHandle, const TemplateParameterNode*, StringHash, StringEqual>&
		tparam_nodes_by_name,
	std::unordered_map<StringHandle, TemplateTypeArg, StringHash, StringEqual>& param_name_to_arg,
	int recursion_depth) {
	const TypeInfo* pattern_info = tryGetTypeInfo(pattern_type.type_index());
	const TypeInfo* concrete_info = tryGetTypeInfo(concrete_type.type_index());
	if (pattern_info == nullptr || concrete_info == nullptr ||
		!pattern_info->isTemplateInstantiation() ||
		!concrete_info->isTemplateInstantiation() ||
		pattern_info->baseTemplateName() != concrete_info->baseTemplateName()) {
		return false;
	}

	bool produced_deduction = false;
	const auto& pattern_args = pattern_info->templateArgs();
	const auto& concrete_args = concrete_info->templateArgs();
	for (size_t i = 0; i < pattern_args.size() && i < concrete_args.size(); ++i) {
		auto nested_result = preDeduceTemplateArgsFromTemplateArgRecursive(
			pattern_args[i],
			concrete_args[i],
			tparam_nodes_by_name,
			param_name_to_arg,
			recursion_depth);
		if (!nested_result.has_value()) {
			return std::nullopt;
		}
		produced_deduction = produced_deduction || *nested_result;
	}

	return produced_deduction;
}

std::optional<Parser::CallArgDeductionInfo> Parser::buildDeductionMapFromCallArgs(
	const InlineVector<TemplateParameterNode, 4>& template_params,
	std::span<const ASTNode> func_params,
	std::span<const TypeSpecifierNode> arg_types,
	int recursion_depth,
	const std::unordered_map<StringHandle, TemplateTypeArg, StringHash, StringEqual>* prebound_template_args) {
	CallArgDeductionInfo deduction_info;
	auto& param_name_to_arg = deduction_info.param_name_to_arg;
	if (prebound_template_args != nullptr) {
		param_name_to_arg = *prebound_template_args;
	}
	auto& pre_deduced_arg_indices = deduction_info.pre_deduced_arg_indices;
	auto& func_param_to_call_arg_index = deduction_info.func_param_to_call_arg_index;

	// Build map of template parameter names for O(1) lookup; also used by the
	// direct-param pre-deduction pass below to identify which function-param
	// types correspond to template type parameters.
	std::unordered_map<StringHandle, const TemplateParameterNode*, StringHash, StringEqual> tparam_nodes_by_name;
	for (const auto& tparam_node : template_params) {
		tparam_nodes_by_name.emplace(tparam_node.nameHandle(), &tparam_node);
	}

	auto getNonTypeTemplateParamCategoryOrInt = [&](StringHandle param_name) -> TypeCategory {
		auto it = tparam_nodes_by_name.find(param_name);
		if (it == tparam_nodes_by_name.end() || it->second->kind() != TemplateParameterKind::NonType ||
			!it->second->has_type()) {
			return TypeCategory::Int;
		}
		return it->second->type_specifier_node().type();
	};

	auto recordPreDeducedArg = [&](StringHandle param_name, const TemplateTypeArg& new_arg,
								  std::string_view kind_label, std::string_view value_label) -> bool {
		auto [it, inserted] = param_name_to_arg.emplace(param_name, new_arg);
		if (!inserted && !(it->second == new_arg)) {
			FLASH_LOG_FORMAT(Templates, Error,
							 "[depth={}]: Conflicting deduction for {} param '{}'",
							 recursion_depth, kind_label, StringTable::getStringView(param_name));
			return false;
		}
		if (inserted) {
			FLASH_LOG_FORMAT(Templates, Debug,
							 "[depth={}]: Pre-deduced {} param '{}' = {}",
							 recursion_depth, kind_label, StringTable::getStringView(param_name), value_label);
		}
		return true;
	};

	auto tryMatchArrayBoundExpression =
		[&](const ASTNode& bound_expr, size_t concrete_bound) -> bool {
		std::unordered_map<TypeIndex, TemplateTypeArg> type_substitution_map;
		std::unordered_map<std::string_view, int64_t> nontype_substitution_map;
		for (const auto& [name, deduced_arg] : param_name_to_arg) {
			if (deduced_arg.is_value) {
				nontype_substitution_map[StringTable::getStringView(name)] = deduced_arg.value;
			}
		}

		ASTNode substituted_expr = substitute_template_params_in_expression(
			bound_expr, type_substitution_map, nontype_substitution_map, StringHandle{});

		if (substituted_expr.is<ExpressionNode>()) {
			const ExpressionNode& substituted_node = substituted_expr.as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(substituted_node)) {
				StringHandle dependent_name = StringTable::getOrInternStringHandle(
					std::get<IdentifierNode>(substituted_node).name());
				auto param_it = tparam_nodes_by_name.find(dependent_name);
				if (param_it != tparam_nodes_by_name.end() &&
					param_it->second->kind() == TemplateParameterKind::NonType) {
					TemplateTypeArg new_arg = TemplateTypeArg::makeValue(
						static_cast<int64_t>(concrete_bound),
						getNonTypeTemplateParamCategoryOrInt(dependent_name));
					return recordPreDeducedArg(
						dependent_name,
						new_arg,
						"non-type",
						std::to_string(static_cast<unsigned long long>(concrete_bound)));
				}
			}
		}

		ConstExpr::EvaluationContext eval_ctx(gSymbolTable, *this);
		eval_ctx.template_environment.bindings.reserve(param_name_to_arg.size());
		for (const auto& [deduced_name, deduced_arg] : param_name_to_arg) {
			TemplateBinding binding;
			binding.name = deduced_name;
			binding.kind = deduced_arg.is_template_template_arg
				? TemplateParameterKind::Template
				: (deduced_arg.is_value ? TemplateParameterKind::NonType : TemplateParameterKind::Type);
			binding.is_pack = false;
			binding.args.push_back(deduced_arg);
			eval_ctx.template_environment.bindings.push_back(std::move(binding));
		}
		auto eval_result = ConstExpr::Evaluator::evaluate(substituted_expr, eval_ctx);
		return eval_result.success() &&
			   eval_result.as_int() == static_cast<int64_t>(concrete_bound);
	};

	auto countRequiredFunctionArgsAfter = [&](size_t start_index) {
		size_t required_args = 0;
		for (size_t param_index = start_index; param_index < func_params.size(); ++param_index) {
			if (!func_params[param_index].is<DeclarationNode>()) {
				continue;
			}
			const auto& param_decl = func_params[param_index].as<DeclarationNode>();
			if (param_decl.is_parameter_pack() || param_decl.has_default_value()) {
				continue;
			}
			++required_args;
		}
		return required_args;
	};
	func_param_to_call_arg_index.assign(func_params.size(), SIZE_MAX);
	size_t call_arg_index = 0;
	size_t abbreviated_auto_param_index = 0;
	for (size_t i = 0; i < func_params.size(); ++i) {
		if (!func_params[i].is<DeclarationNode>()) {
			continue;
		}
		const DeclarationNode& func_param_decl = func_params[i].as<DeclarationNode>();
		const TypeSpecifierNode& direct_fp_type = func_param_decl.type_specifier_node();
		if (direct_fp_type.category() == TypeCategory::Auto) {
			while (abbreviated_auto_param_index < template_params.size() &&
				   template_params[abbreviated_auto_param_index].kind() != TemplateParameterKind::Type) {
				++abbreviated_auto_param_index;
			}
			if (abbreviated_auto_param_index < template_params.size()) {
				deduction_info.positional_deducible_param_names.insert(
					template_params[abbreviated_auto_param_index].nameHandle());
				++abbreviated_auto_param_index;
			}
		}
		StringHandle direct_fp_type_name;
		if (const TypeInfo* direct_type_info = tryGetTypeInfo(direct_fp_type.type_index())) {
			direct_fp_type_name = direct_type_info->name();
		}
		if (!direct_fp_type_name.isValid()) {
			direct_fp_type_name = direct_fp_type.token().handle();
		}
		auto direct_param_it = tparam_nodes_by_name.find(direct_fp_type_name);
		if (direct_param_it != tparam_nodes_by_name.end() &&
			direct_param_it->second->kind() == TemplateParameterKind::Type &&
			direct_fp_type.pointer_depth() == 0) {
			// Only mark a template type parameter as positionally deducible when the
			// function-parameter type is NOT pointer-qualified (i.e. T, T&, const T& etc.).
			// For pointer patterns like U* or U**, the base name U is the underlying type
			// but cannot be deduced positionally without first stripping the extra pointer
			// levels from the call argument.  Positional deduction for these cases would
			// incorrectly bind U=int for a plain-int argument to pick(U*).
			// The legacy pointer-stripping deduction path in tryDeduceCandidate handles
			// these cases correctly, and deduceTemplateCandidateViability will fall back
			// to std::nullopt (SFINAE) when the call arg lacks sufficient pointer depth.
			deduction_info.positional_deducible_param_names.insert(direct_fp_type_name);
		}
		// Detect whether this function parameter is a pack.  The explicit
		// `is_parameter_pack` flag is the primary signal, but for class-template
		// member function templates the inner template's pack parameter flag
		// may not be set on the pattern's DeclarationNode.  Fall back to checking
		// whether the parameter type names a variadic template parameter of the
		// enclosing template, mirroring the pattern used in
		// instantiate_member_function_template_core.
		bool is_pack = isTemplateFunctionParameterPack(template_params, func_param_decl);
		if (is_pack) {
			size_t required_after = countRequiredFunctionArgsAfter(i + 1);
			deduction_info.function_pack_call_arg_start = call_arg_index;
			deduction_info.function_pack_call_arg_end = arg_types.size() >= required_after
														 ? arg_types.size() - required_after
														 : call_arg_index;
			call_arg_index = deduction_info.function_pack_call_arg_end;
			// Record which template-parameter pack this function-parameter pack expands.
			// The type specifier for e.g. "Ts... args" carries "Ts" as either its token
			// value or its TypeInfo name.  This name is later used by the explicit-deduction
			// loop to gate the call-arg-slice size check on only the matching template pack.
			{
				const TypeSpecifierNode& fp_type = func_param_decl.type_specifier_node();
				// Prioritise TypeInfo::name() over the token handle: for class-template
				// inner member function template pack parameters the token handle is
				// often invalid/empty, while the TypeInfo name is always populated.
				// This matches the priority order used in the detection block above.
				StringHandle pack_type_name;
				if (const TypeInfo* ti = tryGetTypeInfo(fp_type.type_index())) {
					pack_type_name = ti->name();
				}
				if (!pack_type_name.isValid()) {
					pack_type_name = fp_type.token().handle();
				}
				if (!tparam_nodes_by_name.count(pack_type_name)) {
					pack_type_name = {};
				}
				collectDependentTemplateParamNamesFromType(
					fp_type.type_index(),
					tparam_nodes_by_name,
					pack_type_name,
					deduction_info.function_pack_dependent_param_names);
				if (pack_type_name.isValid()) {
					deduction_info.function_pack_dependent_param_names.insert(pack_type_name);
				}
				deduction_info.function_pack_template_param_name = pack_type_name;
				deduction_info.function_pack_element_type_index = fp_type.type_index();
			}
			continue;
		}
		if (call_arg_index >= arg_types.size()) {
			break;
		}
		func_param_to_call_arg_index[i] = call_arg_index++;
	}

	for (size_t i = 0; i < func_params.size(); ++i) {
		if (!func_params[i].is<DeclarationNode>())
			continue;
		size_t concrete_arg_index = func_param_to_call_arg_index[i];
		if (concrete_arg_index == SIZE_MAX || concrete_arg_index >= arg_types.size()) {
			continue;
		}
		const DeclarationNode& func_param_decl = func_params[i].as<DeclarationNode>();
		const TypeSpecifierNode& fp_type = func_param_decl.type_specifier_node();
		const TypeSpecifierNode& ca_type = arg_types[concrete_arg_index];
		// If both the function parameter and the call argument are struct template
		// instantiations of the same base template, match their template args pairwise
		// to deduce any template parameters that appear as dependent entries.
		TypeIndex fp_idx = fp_type.type_index();
		TypeIndex ca_idx = ca_type.type_index();
		const TypeInfo* fp_info = tryGetTypeInfo(fp_idx);
		const TypeInfo* ca_info = tryGetTypeInfo(ca_idx);
		if (fp_info && ca_info &&
			fp_info->isTemplateInstantiation() && ca_info->isTemplateInstantiation() &&
			fp_info->baseTemplateName() == ca_info->baseTemplateName()) {
			auto slot_produced_deduction = preDeduceTemplateArgsFromMatchingTypes(
				fp_type,
				ca_type,
				tparam_nodes_by_name,
				param_name_to_arg,
				recursion_depth);
			if (!slot_produced_deduction.has_value()) {
				return std::nullopt;
			}
			if (*slot_produced_deduction) {
				pre_deduced_arg_indices.insert(concrete_arg_index);
			}
		}

		if (func_param_decl.is_array() && ca_type.is_array()) {
			const size_t pattern_dim_count = func_param_decl.array_dimension_count();
			const size_t concrete_dim_count = ca_type.array_dimension_count();
			if (pattern_dim_count <= concrete_dim_count) {
				bool slot_produced_deduction = false;

				TypeIndex array_fp_idx = fp_type.type_index();
				if (const TypeInfo* fp_type_info = tryGetTypeInfo(array_fp_idx)) {
					StringHandle fp_name = fp_type_info->name();
					auto param_it = tparam_nodes_by_name.find(fp_name);
					if (param_it != tparam_nodes_by_name.end() &&
						param_it->second->kind() == TemplateParameterKind::Type &&
						!param_name_to_arg.count(fp_name)) {
						TypeSpecifierNode deduced_element_type = ca_type;
						deduced_element_type.set_reference_qualifier(ReferenceQualifier::None);
						const auto& concrete_dims = ca_type.array_dimensions();
						if (pattern_dim_count <= concrete_dims.size()) {
							std::vector<size_t> remaining_dims(
								concrete_dims.begin() + pattern_dim_count,
								concrete_dims.end());
							deduced_element_type.set_array_dimensions(remaining_dims);
							TemplateTypeArg new_arg = TemplateTypeArg::makeTypeSpecifier(deduced_element_type);
							if (!recordPreDeducedArg(fp_name, new_arg, "type", new_arg.toString())) {
								return std::nullopt;
							}
							slot_produced_deduction = true;
						}
					}
				}

				bool all_dims_matched = true;
				const auto& pattern_dims = func_param_decl.array_dimensions();
				const auto& concrete_dims = ca_type.array_dimensions();
				for (size_t dim_index = 0; dim_index < pattern_dim_count; ++dim_index) {
					if (!tryMatchArrayBoundExpression(pattern_dims[dim_index], concrete_dims[dim_index])) {
						all_dims_matched = false;
						break;
					}
					slot_produced_deduction = true;
				}

				if (!all_dims_matched) {
					return std::nullopt;
				}
				if (slot_produced_deduction) {
					pre_deduced_arg_indices.insert(concrete_arg_index);
				}
			}
		}
	}

	// Handle the case where a function parameter IS directly a non-variadic template type
	// parameter (e.g., template<typename T> T func(Widget& w, T b) — T is the 2nd param).
	// Without this, the main loop would naively consume the 1st call argument for T.
	// Deduce non-variadic template type parameters directly from function parameter types.
	// For example, in template<typename T, typename U, typename... Rest> func(T, U, Rest...),
	// T and U can be pre-deduced from call args 0 and 1 respectively.
	// IMPORTANT: Skip variadic FUNCTION parameter packs — those are handled by the main
	// loop's pack-fill path and must not be pre-consumed here.
	// NOTE: We no longer gate this on !has_variadic_tparam. When a template has a variadic
	// type parameter, non-pack function params that directly correspond to non-pack template
	// params can still be safely pre-deduced; only the pack function param slots are skipped.
	for (size_t i = 0; i < func_params.size(); ++i) {
		if (!func_params[i].is<DeclarationNode>())
			continue;
		const DeclarationNode& fp_decl = func_params[i].as<DeclarationNode>();
		// Skip parameter packs — their deduction is handled by the main loop.
		if (fp_decl.is_parameter_pack())
			continue;
		size_t concrete_arg_index = func_param_to_call_arg_index[i];
		if (concrete_arg_index == SIZE_MAX || concrete_arg_index >= arg_types.size()) {
			continue;
		}
		const TypeSpecifierNode& fp_type = fp_decl.type_specifier_node();
		const TypeSpecifierNode& ca_type = arg_types[concrete_arg_index];

		// Deduce through pointer-qualified parameters such as P = T* and A = int*.
		if (fp_type.pointer_depth() > 0 && !fp_type.is_array() && !fp_decl.is_array()) {
			TypeIndex fp_idx = fp_type.type_index();
			const TypeInfo* fp_type_info = tryGetTypeInfo(fp_idx);
			StringHandle fp_name = fp_type_info != nullptr ? fp_type_info->name() : StringHandle{};
			if (!fp_name.isValid()) {
				fp_name = fp_type.token().handle();
			}
			auto param_it = tparam_nodes_by_name.find(fp_name);
			if (param_it != tparam_nodes_by_name.end() &&
				param_it->second->kind() == TemplateParameterKind::Type &&
				!param_name_to_arg.count(fp_name)) {
				if (ca_type.pointer_depth() < fp_type.pointer_depth()) {
					return std::nullopt;
				}
				TemplateTypeArg new_arg = TemplateTypeArg::makeTypeSpecifier(ca_type);
				new_arg.pointer_depth =
					static_cast<uint8_t>(new_arg.pointer_depth - fp_type.pointer_depth());
				if (!new_arg.pointer_cv_qualifiers.empty()) {
					std::vector<CVQualifier> remaining_pointer_cv;
					const size_t remove_count = std::min<size_t>(
						fp_type.pointer_depth(),
						new_arg.pointer_cv_qualifiers.size());
					for (size_t cv_index = remove_count;
						 cv_index < new_arg.pointer_cv_qualifiers.size();
						 ++cv_index) {
						remaining_pointer_cv.push_back(new_arg.pointer_cv_qualifiers[cv_index]);
					}
					new_arg.pointer_cv_qualifiers = std::move(remaining_pointer_cv);
				}
				if (fp_type.reference_qualifier() == ReferenceQualifier::None) {
					new_arg.ref_qualifier = ReferenceQualifier::None;
				}
				if (!recordPreDeducedArg(fp_name, new_arg, "type", new_arg.toString())) {
					return std::nullopt;
				}
				pre_deduced_arg_indices.insert(concrete_arg_index);
				continue;
			}
		}

		// Only handle directly-typed params (pointer_depth 0 covers T, T&, const T&).
		// Pointer-to-template (T*) cases are handled via substitution elsewhere.
		// Array declarators use a separate deduction path so T in T(&)[N] binds to the
		// element type instead of the whole array type.
		if (fp_type.pointer_depth() != 0 || fp_type.is_array() || fp_decl.is_array())
			continue;

		TypeIndex fp_idx = fp_type.type_index();
		const TypeInfo* fp_type_info = tryGetTypeInfo(fp_idx);
		if (!fp_type_info)
			continue;

		StringHandle fp_name = fp_type_info->name();
		if (!tparam_nodes_by_name.count(fp_name))
			continue;  // not a template parameter
		if (param_name_to_arg.count(fp_name))
			continue;  // already deduced

		// Deduce: fp_name -> ca_type (call argument type for this parameter slot)
		TemplateTypeArg new_arg = TemplateTypeArg::makeTypeSpecifier(ca_type);
		if (fp_type.reference_qualifier() == ReferenceQualifier::None) {
			// C++20 [temp.deduct.call]: for by-value parameters, top-level cv/ref on the
			// call argument do not participate in deduction. Keep the underlying type
			// identity, but drop the argument's top-level reference/cv wrapper.
			new_arg.ref_qualifier = ReferenceQualifier::None;
			new_arg.cv_qualifier = CVQualifier::None;
		} else if (fp_type.reference_qualifier() == ReferenceQualifier::LValueReference) {
			// For plain lvalue-reference parameters (T&, const T&), deduction binds T to the
			// referred-to type rather than to a reference type. The parameter's own cv on the
			// referred-to type is ignored, but cv carried by a plain T& argument participates.
			new_arg.ref_qualifier = ReferenceQualifier::None;
			const auto argument_cv = static_cast<uint8_t>(new_arg.cv_qualifier);
			const auto parameter_cv = static_cast<uint8_t>(fp_type.cv_qualifier());
			new_arg.cv_qualifier = static_cast<CVQualifier>(argument_cv & ~parameter_cv);
		} else if (fp_type.reference_qualifier() == ReferenceQualifier::RValueReference) {
			const bool is_forwarding_reference =
				fp_type.cv_qualifier() == CVQualifier::None;
			if (is_forwarding_reference) {
				// Forwarding-reference deduction needs the full T&& vs lvalue/rvalue rules.
				// Let the main deduction path handle it instead of pre-deducing the wrong shape.
				continue;
			}
			new_arg.ref_qualifier = ReferenceQualifier::None;
			const auto argument_cv = static_cast<uint8_t>(new_arg.cv_qualifier);
			const auto parameter_cv = static_cast<uint8_t>(fp_type.cv_qualifier());
			new_arg.cv_qualifier = static_cast<CVQualifier>(argument_cv & ~parameter_cv);
		}
		param_name_to_arg.emplace(fp_name, new_arg);
		pre_deduced_arg_indices.insert(concrete_arg_index);
		FLASH_LOG_FORMAT(Templates, Debug,
						 "[depth={}]: Direct-param pre-deduced type param '{}' = type {} from func param {} / call arg {}",
						 recursion_depth, StringTable::getStringView(fp_name),
						 static_cast<int>(ca_type.type()), i, concrete_arg_index);
	}

	return deduction_info;
}

std::optional<Parser::CallArgDeductionInfo> Parser::buildDeductionMapFromCallArgs(
	const InlineVector<TemplateParameterNode, 4>& template_params,
	const FunctionDeclarationNode& func_decl,
	std::span<const TypeSpecifierNode> arg_types,
	int recursion_depth,
	const std::unordered_map<StringHandle, TemplateTypeArg, StringHash, StringEqual>* prebound_template_args) {
	return buildDeductionMapFromCallArgs(
		template_params,
		func_decl.parameter_nodes(),
		arg_types,
		recursion_depth,
		prebound_template_args);
}

bool Parser::functionTemplateAcceptsCallArgumentCount(
	std::span<const TemplateParameterNode> template_params,
	const FunctionDeclarationNode& func_decl,
	size_t argument_count) {
	const size_t min_required_args = countMinRequiredArgs(func_decl);
	if (argument_count < min_required_args) {
		return false;
	}
	if (func_decl.is_variadic()) {
		return true;
	}

	for (const auto& param_node : func_decl.parameter_nodes()) {
		if (!param_node.is<DeclarationNode>()) {
			continue;
		}
		if (isTemplateFunctionParameterPack(template_params, param_node.as<DeclarationNode>())) {
			return true;
		}
	}

	return argument_count <= func_decl.parameter_nodes().size();
}

bool Parser::materializeTemplateFunctionParameters(
	FunctionDeclarationNode& new_func_ref,
	const FunctionTemplateInstantiationContext& instantiation_context,
	const FunctionTemplateBindingData& binding_data,
	FunctionTemplateInstantiationFlags instantiation_flags) {
	const FunctionDeclarationNode& func_decl = instantiation_context.func_decl;
	std::span<const TemplateParameterNode> template_params = instantiation_context.template_params;
	std::span<const TemplateTypeArg> template_args = instantiation_context.template_args;
	const std::span<const TypeSpecifierNode>* arg_types = binding_data.arg_types;
	const std::span<const size_t>* template_param_arg_starts = binding_data.template_param_arg_starts;
	const std::span<const size_t>* template_param_arg_counts = binding_data.template_param_arg_counts;
	const std::optional<CallArgDeductionInfo>* deduction_info = binding_data.deduction_info;
	const int recursion_depth = instantiation_context.recursion_depth;
	const bool use_explicit_materialization =
		hasInstantiationFlag(instantiation_flags, FunctionTemplateInstantiationFlags::ExplicitMaterialization);
	size_t arg_type_index = 0;
	if (use_explicit_materialization) {
		if (template_param_arg_starts == nullptr || template_param_arg_counts == nullptr) {
			return false;
		}
		struct PackBinding {
			size_t start_index;
			size_t count;
		};
		auto getPackParameterName = [&](const TypeSpecifierNode& type_spec) -> std::string_view {
			if (deduction_info != nullptr && deduction_info->has_value() &&
				(*deduction_info)->function_pack_template_param_name.isValid()) {
				return StringTable::getStringView((*deduction_info)->function_pack_template_param_name);
			}
			if (!type_spec.token().value().empty()) {
				return type_spec.token().value();
			}
			if (const TypeInfo* type_info = tryGetTypeInfo(type_spec.type_index())) {
				return StringTable::getStringView(type_info->name());
			}
			return {};
		};
		auto cloneNonVariadicTemplateParam = [&](const TemplateParameterNode& param) {
			TemplateParameterNode clone = [&]() {
				switch (param.kind()) {
					case TemplateParameterKind::Type:
						return TemplateParameterNode(param.nameHandle(), param.token());
					case TemplateParameterKind::NonType:
						return TemplateParameterNode(param.nameHandle(), param.type_specifier_node(), param.token());
					case TemplateParameterKind::Template:
						return TemplateParameterNode(param.nameHandle(), param.nested_parameters(), param.token());
				}
				return TemplateParameterNode(param.nameHandle(), param.token());
			}();
			if (param.has_concept_constraint()) {
				clone.set_concept_constraint(param.concept_constraint());
			}
			if (param.has_concept_args()) {
				const std::span<const ASTNode> concept_args = param.concept_args();
				clone.set_concept_args(std::vector<ASTNode>(concept_args.begin(), concept_args.end()));
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
		auto getTemplateParamPackBinding = [&](std::string_view pack_param_name) -> std::optional<PackBinding> {
			for (size_t template_param_index = 0; template_param_index < template_params.size(); ++template_param_index) {
				const TemplateParameterNode* template_param_ptr = tryGetTemplateParameterNode(template_params[template_param_index]);
				if (template_param_ptr == nullptr) {
					continue;
				}
				const auto& template_param = *template_param_ptr;
				if (template_param.is_variadic() && template_param.name() == pack_param_name) {
					return PackBinding{
						(*template_param_arg_starts)[template_param_index],
						(*template_param_arg_counts)[template_param_index]};
				}
			}
			return std::nullopt;
		};
		auto buildPackElementParamName = [&](const DeclarationNode& declaration, size_t pack_element_offset) {
			StringBuilder param_name_builder;
			param_name_builder.append(declaration.identifier_token().value());
			param_name_builder.append('_');
			param_name_builder.append(pack_element_offset);
			return param_name_builder.commit();
		};
		auto buildSubstitutionForPackElement =
			[&](std::string_view pack_param_name, size_t pack_element_offset,
				InlineVector<ASTNode, 4>& subst_params,
				InlineVector<TemplateTypeArg, 4>& subst_args) -> bool {
				auto pack_binding = getTemplateParamPackBinding(pack_param_name);
				if (!pack_binding.has_value() || pack_element_offset >= pack_binding->count) {
					return false;
				}
				size_t template_arg_index = 0;
				for (size_t template_param_index = 0; template_param_index < template_params.size(); ++template_param_index) {
					const TemplateParameterNode* template_param_ptr = tryGetTemplateParameterNode(template_params[template_param_index]);
					if (template_param_ptr == nullptr) {
						continue;
					}
					const auto& template_param = *template_param_ptr;
					if (template_param.is_variadic()) {
						const bool is_primary = (template_param.name() == pack_param_name);
						const bool is_co_pack = deduction_info != nullptr && deduction_info->has_value() && !is_primary &&
							(*deduction_info)->function_pack_dependent_param_names.count(template_param.nameHandle());
						if (is_primary || is_co_pack) {
							if (template_arg_index + pack_element_offset < template_args.size()) {
								subst_params.push_back(emplace_node<TemplateParameterNode>(
									cloneNonVariadicTemplateParam(template_param)));
								subst_args.push_back(template_args[template_arg_index + pack_element_offset]);
							}
						}
						template_arg_index += (*template_param_arg_counts)[template_param_index];
						continue;
					}
					if (template_arg_index >= template_args.size()) {
						break;
					}
					subst_params.push_back(emplace_node<TemplateParameterNode>(template_param));
					subst_args.push_back(template_args[template_arg_index]);
					++template_arg_index;
				}
				return true;
			};
		auto buildMaterializedParamType =
			[&](const DeclarationNode& original_param_decl,
				const InlineVector<ASTNode, 4>& materialized_template_params,
				const InlineVector<TemplateTypeArg, 4>& materialized_template_args) {
				const TypeSpecifierNode& original_param_type = original_param_decl.type_specifier_node();
				InlineVector<TemplateParameterNode, 4> typed_params;
				typed_params.reserve(materialized_template_params.size());
				for (const ASTNode& param_node : materialized_template_params) {
					if (const TemplateParameterNode* typed_param = tryGetTemplateParameterNode(param_node);
						typed_param != nullptr) {
						typed_params.push_back(*typed_param);
					}
				}

				TypeSpecifierNode substituted_param_type = buildSubstitutedTypeSpecifier(
					original_param_type,
					original_param_decl.type_node(),
					original_param_decl.identifier_token(),
					typed_params,
					materialized_template_args,
					[this](const ASTNode& node, const auto& params, const auto& args) {
						return substituteTemplateParameters(node, params, args);
					},
					[this](const TypeSpecifierNode& type_spec, const auto& params, const auto& args) {
						return substitute_template_parameter(type_spec, params, args);
					},
					nullptr,
					TypeIndex{},
					TypeIndex{},
					false,
					true);
				ASTNode param_type = emplace_node<TypeSpecifierNode>(substituted_param_type);
				resolveDependentMemberAlias(param_type, typed_params, materialized_template_args);
				normalizeSubstitutedTypeSpec(param_type.as<TypeSpecifierNode>());
				return param_type;
			};

		for (size_t i = 0; i < func_decl.parameter_nodes().size(); ++i) {
			const auto& param = func_decl.parameter_nodes()[i];
			if (!param.is<DeclarationNode>()) {
				continue;
			}
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			const TypeSpecifierNode& orig_param_type = param_decl.type_specifier_node();
			if (param_decl.is_parameter_pack()) {
				size_t pack_start_index = arg_type_index;
				std::string_view pack_param_name = getPackParameterName(orig_param_type);
				auto pack_binding = getTemplateParamPackBinding(pack_param_name);
				if (pack_binding.has_value()) {
					for (size_t pack_element_offset = 0; pack_element_offset < pack_binding->count; ++pack_element_offset) {
						InlineVector<ASTNode, 4> subst_params;
						InlineVector<TemplateTypeArg, 4> subst_args;
						if (!buildSubstitutionForPackElement(pack_param_name, pack_element_offset, subst_params, subst_args)) {
							continue;
						}
						ASTNode param_type = buildMaterializedParamType(param_decl, subst_params, subst_args);
						std::string_view param_name = buildPackElementParamName(param_decl, pack_element_offset);
						Token param_token(Token::Type::Identifier,
										  param_name,
										  param_decl.identifier_token().line(),
										  param_decl.identifier_token().column(),
										  param_decl.identifier_token().file_index());
						auto new_param_decl = emplace_node<DeclarationNode>(param_type, param_token);
						new_func_ref.add_parameter_node(new_param_decl);
						arg_type_index++;
					}
				}
				size_t pack_size = arg_type_index - pack_start_index;
				pack_param_info_.push_back({param_decl.identifier_token().value(), pack_start_index, pack_size});
				continue;
			}

			InlineVector<ASTNode, 4> flat_subst_params;
			InlineVector<TemplateTypeArg, 4> flat_subst_args;
			{
				size_t flat_arg_idx = 0;
				for (size_t j = 0; j < template_params.size(); ++j) {
					const TemplateParameterNode* tp = tryGetTemplateParameterNode(template_params[j]);
					if (!tp) {
						continue;
					}
					const size_t count = (*template_param_arg_counts)[j];
					if (tp->is_variadic()) {
						for (size_t k = 0; k < count; ++k) {
							if (flat_arg_idx + k < template_args.size()) {
								flat_subst_params.push_back(emplace_node<TemplateParameterNode>(
									cloneNonVariadicTemplateParam(*tp)));
								flat_subst_args.push_back(template_args[flat_arg_idx + k]);
							}
						}
						flat_arg_idx += count;
						continue;
					}
					if (flat_arg_idx < template_args.size()) {
						flat_subst_params.push_back(emplace_node<TemplateParameterNode>(*tp));
						flat_subst_args.push_back(template_args[flat_arg_idx]);
					}
					++flat_arg_idx;
				}
			}
			ASTNode param_type = buildMaterializedParamType(param_decl, flat_subst_params, flat_subst_args);
			auto new_param_decl = emplace_node<DeclarationNode>(param_type, param_decl.identifier_token());
			if (param_decl.has_default_value()) {
				new_param_decl.as<DeclarationNode>().set_default_value(param_decl.default_value());
			}
			new_func_ref.add_parameter_node(new_param_decl);
			arg_type_index++;
		}
		return true;
	}

	if (arg_types == nullptr) {
		return false;
	}
	for (size_t i = 0; i < func_decl.parameter_nodes().size(); ++i) {
		const auto& param = func_decl.parameter_nodes()[i];
		if (!param.is<DeclarationNode>()) {
			continue;
		}
		const DeclarationNode& param_decl = param.as<DeclarationNode>();
		if (param_decl.is_parameter_pack()) {
			size_t pack_start_index = arg_type_index;
			const TypeSpecifierNode& orig_param_type = param_decl.type_specifier_node();
			bool is_forwarding_reference = orig_param_type.is_rvalue_reference();
			while (arg_type_index < arg_types->size()) {
				const TypeSpecifierNode& arg_type = (*arg_types)[arg_type_index];
				ASTNode param_type = emplace_node<TypeSpecifierNode>(
					arg_type.type(),
					arg_type.qualifier(),
					arg_type.size_in_bits(),
					Token(), CVQualifier::None);
				param_type.as<TypeSpecifierNode>().set_type_index(arg_type.type_index());
				if (is_forwarding_reference) {
					if (arg_type.is_lvalue_reference()) {
						param_type.as<TypeSpecifierNode>().set_reference_qualifier(ReferenceQualifier::LValueReference);
					} else if (arg_type.is_rvalue_reference()) {
						param_type.as<TypeSpecifierNode>().set_reference_qualifier(ReferenceQualifier::RValueReference);
					} else {
						param_type.as<TypeSpecifierNode>().set_reference_qualifier(ReferenceQualifier::RValueReference);
					}
				}
				for (const auto& ptr_level : arg_type.pointer_levels()) {
					param_type.as<TypeSpecifierNode>().add_pointer_level(ptr_level.cv_qualifier);
				}
				StringBuilder param_name_builder;
				param_name_builder.append(param_decl.identifier_token().value());
				param_name_builder.append('_');
				param_name_builder.append(arg_type_index - pack_start_index);
				std::string_view param_name = param_name_builder.commit();
				Token param_token(Token::Type::Identifier,
								  param_name,
								  param_decl.identifier_token().line(),
								  param_decl.identifier_token().column(),
								  param_decl.identifier_token().file_index());
				auto new_param_decl = emplace_node<DeclarationNode>(param_type, param_token);
				new_func_ref.add_parameter_node(new_param_decl);
				arg_type_index++;
			}
			size_t pack_size = arg_type_index - pack_start_index;
			pack_param_info_.push_back({param_decl.identifier_token().value(), pack_start_index, pack_size});
			continue;
		}

		const TypeSpecifierNode& orig_param_type = param_decl.type_specifier_node();
		ASTNode param_type;
		if (orig_param_type.category() == TypeCategory::Auto && arg_type_index < arg_types->size()) {
			const TypeSpecifierNode& deduced_arg_type = (*arg_types)[arg_type_index];
			CVQualifier cv = static_cast<CVQualifier>(
				static_cast<uint8_t>(deduced_arg_type.cv_qualifier()) |
				static_cast<uint8_t>(orig_param_type.cv_qualifier()));
			param_type = emplace_node<TypeSpecifierNode>(
				deduced_arg_type.type(),
				TypeQualifier::None,
				deduced_arg_type.size_in_bits(),
				orig_param_type.token(),
				cv);
			param_type.as<TypeSpecifierNode>().set_type_index(deduced_arg_type.type_index());
			if (deduced_arg_type.has_function_signature()) {
				param_type.as<TypeSpecifierNode>().set_function_signature(deduced_arg_type.function_signature());
			}
			for (const auto& ptr_level : deduced_arg_type.pointer_levels()) {
				param_type.as<TypeSpecifierNode>().add_pointer_level(ptr_level.cv_qualifier);
			}
			if (orig_param_type.pointer_depth() > deduced_arg_type.pointer_depth()) {
				const auto& orig_levels = orig_param_type.pointer_levels();
				for (size_t pl = deduced_arg_type.pointer_depth(); pl < orig_param_type.pointer_depth(); ++pl) {
					param_type.as<TypeSpecifierNode>().add_pointer_level(orig_levels[pl].cv_qualifier);
				}
			}
		} else {
			TypeIndex subst_type_index = substitute_template_parameter(
				orig_param_type, template_params, template_args);
			if (subst_type_index.category() == TypeCategory::UserDefined &&
				subst_type_index == orig_param_type.type_index() &&
				tryGetTypeInfo(subst_type_index) &&
				tryGetTypeInfo(subst_type_index)->isTemplateInstantiation() &&
				i < arg_types->size() &&
				(*arg_types)[i].category() == TypeCategory::Struct) {
				subst_type_index.setCategory(TypeCategory::Struct);
				subst_type_index = (*arg_types)[i].type_index();
				FLASH_LOG_FORMAT(Templates, Debug,
								 "[depth={}]: Using call-site Struct type_index={} for dependent-placeholder param",
								 recursion_depth, subst_type_index);
			}
			param_type = emplace_node<TypeSpecifierNode>(
				subst_type_index.category(),
				TypeQualifier::None,
				get_type_size_bits(subst_type_index.category()),
				orig_param_type.token(),
				orig_param_type.cv_qualifier());
			param_type.as<TypeSpecifierNode>().set_type_index(subst_type_index);
			applyTemplateArgIndirection(param_type.as<TypeSpecifierNode>(), orig_param_type, template_params, template_args,
										/*propagate_reference_qualifier=*/false);
			propagateFunctionSignatureFromTemplateArg(
				param_type.as<TypeSpecifierNode>(),
				orig_param_type,
				subst_type_index,
				template_params,
				template_args);
			for (const auto& ptr_level : orig_param_type.pointer_levels()) {
				param_type.as<TypeSpecifierNode>().add_pointer_level(ptr_level.cv_qualifier);
			}
		}

		if (orig_param_type.is_rvalue_reference() && arg_type_index < arg_types->size()) {
			const TypeSpecifierNode& arg_type = (*arg_types)[arg_type_index];
			if (arg_type.is_lvalue_reference()) {
				param_type.as<TypeSpecifierNode>().set_reference_qualifier(ReferenceQualifier::LValueReference);
			} else if (arg_type.is_rvalue_reference()) {
				param_type.as<TypeSpecifierNode>().set_reference_qualifier(ReferenceQualifier::RValueReference);
			} else if (arg_type.is_reference()) {
				param_type.as<TypeSpecifierNode>().set_reference_qualifier(arg_type.reference_qualifier());
			} else {
				param_type.as<TypeSpecifierNode>().set_reference_qualifier(ReferenceQualifier::RValueReference);
			}
		} else if (orig_param_type.is_lvalue_reference()) {
			param_type.as<TypeSpecifierNode>().set_reference_qualifier(ReferenceQualifier::LValueReference);
		} else if (orig_param_type.is_rvalue_reference()) {
			param_type.as<TypeSpecifierNode>().set_reference_qualifier(ReferenceQualifier::RValueReference);
		}

		resolveDependentMemberAlias(param_type, template_params, template_args);
		TypeSpecifierNode& resolved_param_type = param_type.as<TypeSpecifierNode>();
		const ResolvedAliasTypeInfo param_alias_info = resolveAliasTypeInfo(resolved_param_type.type_index());
		TypeIndex resolved_param_alias_index = param_alias_info.type_index;
		if (!resolved_param_alias_index.is_valid() && param_alias_info.typeEnum() != TypeCategory::Invalid) {
			resolved_param_alias_index = nativeTypeIndex(param_alias_info.typeEnum());
			if (!resolved_param_alias_index.is_valid()) {
				resolved_param_alias_index = TypeIndex{0, param_alias_info.typeEnum()};
			}
		}
		if (resolved_param_alias_index.category() != TypeCategory::Invalid &&
			(resolved_param_alias_index != resolved_param_type.type_index() ||
			 resolved_param_type.category() != param_alias_info.typeEnum())) {
			resolved_param_type.set_type_index(resolved_param_alias_index.withCategory(param_alias_info.typeEnum()));
			resolved_param_type.set_category(param_alias_info.typeEnum());
		}
		resolved_param_type.add_pointer_levels(static_cast<int>(param_alias_info.pointer_depth));
		if (resolved_param_type.reference_qualifier() == ReferenceQualifier::None &&
			param_alias_info.reference_qualifier != ReferenceQualifier::None) {
			resolved_param_type.set_reference_qualifier(param_alias_info.reference_qualifier);
		}
		if (!resolved_param_type.has_function_signature() && param_alias_info.function_signature.has_value()) {
			resolved_param_type.set_function_signature(*param_alias_info.function_signature);
		}
		if (const int resolved_size_bits = getTypeSpecSizeBits(resolved_param_type); resolved_size_bits > 0) {
			resolved_param_type.set_size_in_bits(resolved_size_bits);
		}

		auto new_param_decl = emplace_node<DeclarationNode>(param_type, param_decl.identifier_token());
		if (param_decl.has_default_value()) {
			new_param_decl.as<DeclarationNode>().set_default_value(param_decl.default_value());
		}
		new_func_ref.add_parameter_node(new_param_decl);
		if (arg_type_index < arg_types->size()) {
			arg_type_index++;
		}
	}
	return true;
}

std::optional<ASTNode> Parser::finalizeInstantiatedFunction(
	ASTNode new_func_node,
	FunctionDeclarationNode& new_func_ref,
	const FunctionTemplateInstantiationContext& instantiation_context,
	std::string_view mangled_name,
	FunctionTemplateInstantiationFlags instantiation_flags) {
	const FunctionDeclarationNode& func_decl = instantiation_context.func_decl;
	std::string_view template_name = instantiation_context.template_name;
	const FlashCpp::TemplateInstantiationKey& key = instantiation_context.key;
	const uintptr_t overload_id = instantiation_context.overload_id;
	const bool cacheable_instantiation =
		hasInstantiationFlag(instantiation_flags, FunctionTemplateInstantiationFlags::CacheableInstantiation);
	const bool commit_instantiation =
		hasInstantiationFlag(instantiation_flags, FunctionTemplateInstantiationFlags::CommitInstantiation);
	const bool register_instantiation =
		hasInstantiationFlag(instantiation_flags, FunctionTemplateInstantiationFlags::RegisterInstantiation);
	const bool memoize_body_reparse_failure =
		hasInstantiationFlag(instantiation_flags, FunctionTemplateInstantiationFlags::MemoizeBodyReparseFailure);
	const bool run_inline_heuristic =
		hasInstantiationFlag(instantiation_flags, FunctionTemplateInstantiationFlags::RunInlineHeuristic);
	const bool use_explicit_materialization =
		hasInstantiationFlag(instantiation_flags, FunctionTemplateInstantiationFlags::ExplicitMaterialization);
	const bool skip_body_materialization =
		hasInstantiationFlag(instantiation_flags, FunctionTemplateInstantiationFlags::SkipBodyMaterialization);
	copy_function_properties(new_func_ref, func_decl);

	if (run_inline_heuristic) {
		const auto& func_definition = new_func_ref.get_definition();
		if (!func_definition.has_value()) {
			new_func_ref.set_inline_always(true);
			FLASH_LOG(Templates, Debug, "Marked template instantiation as inline_always (no body): ",
					  new_func_ref.decl_node().identifier_token().value());
		} else if (func_definition->is<BlockNode>()) {
			const BlockNode& block = func_definition->as<BlockNode>();
			const auto& statements = block.get_statements();
			const bool is_pure_expr = std::invoke([&statements]() -> bool {
				bool inner_is_pure_expr = true;
				bool has_pure_return = false;
				statements.visit([&](const ASTNode& stmt) {
					if (stmt.is<TypedefDeclarationNode>()) {
					} else if (stmt.is<ReturnStatementNode>()) {
						const ReturnStatementNode& ret_stmt = stmt.as<ReturnStatementNode>();
						const auto& expr_opt = ret_stmt.expression();
						if (expr_opt.has_value() && expr_opt->is<ExpressionNode>()) {
							const ExpressionNode& expr = expr_opt->as<ExpressionNode>();
							std::visit([&](const auto& e) {
								using T = std::decay_t<decltype(e)>;
								if constexpr (std::is_same_v<T, StaticCastNode> ||
											  std::is_same_v<T, ReinterpretCastNode> ||
											  std::is_same_v<T, ConstCastNode> ||
											  std::is_same_v<T, IdentifierNode>) {
									has_pure_return = true;
								}
							},
									   expr);
						}
					} else {
						inner_is_pure_expr = false;
					}
				});
				inner_is_pure_expr &= static_cast<int>(has_pure_return);
				return inner_is_pure_expr;
			});
			new_func_ref.set_inline_always(is_pure_expr);
			if (is_pure_expr) {
				FLASH_LOG(Templates, Debug, "Marked template instantiation as inline_always (pure expression): ",
						  new_func_ref.decl_node().identifier_token().value());
			} else {
				FLASH_LOG(Templates, Debug, "Template instantiation has computation/side effects (not inlining): ",
						  new_func_ref.decl_node().identifier_token().value());
			}
		}
	}

	if (new_func_ref.is_materialized()) {
		finalize_function_after_definition(new_func_ref);
	} else {
		compute_and_set_mangled_name(new_func_ref);
	}

	if (register_instantiation) {
		if (use_explicit_materialization) {
			gTemplateRegistry.registerInstantiation(key, new_func_node);
		} else if (cacheable_instantiation && commit_instantiation) {
			gTemplateRegistry.registerInstantiation(key, new_func_node);
		}
	}

	if ((commit_instantiation || use_explicit_materialization) &&
		!skip_body_materialization) {
		gSymbolTable.insertGlobal(mangled_name, new_func_node);
	}

	if (use_explicit_materialization) {
		if (new_func_ref.is_materialized() && !functionHasUnresolvedPlaceholderSignature(new_func_ref)) {
			registerAndNormalizeLateMaterializedTopLevelNode(new_func_node);
		}
		return new_func_node;
	}

	const auto& func_definition = new_func_ref.get_definition();
	const bool has_unresolved_signature = functionHasUnresolvedPlaceholderSignature(new_func_ref);
	FLASH_LOG_FORMAT(Templates, Debug,
		"'{}': has_body={}, has_unresolved_signature={}, registering={}",
		template_name, func_definition.has_value(), has_unresolved_signature,
		func_definition.has_value() && !has_unresolved_signature && commit_instantiation);
	if (func_definition.has_value() && !has_unresolved_signature && commit_instantiation) {
		registerAndNormalizeLateMaterializedTopLevelNode(new_func_node);
	}

	(void)memoize_body_reparse_failure;
	(void)overload_id;
	return new_func_node;
}

std::optional<ASTNode> Parser::instantiateBoundFunctionTemplate(
	const FunctionTemplateInstantiationContext& instantiation_context,
	const FunctionTemplateBindingData& binding_data,
	FunctionTemplateInstantiationFlags instantiation_flags) {
	std::string_view template_name = instantiation_context.template_name;
	const FunctionDeclarationNode& func_decl = instantiation_context.func_decl;
	std::span<const TemplateParameterNode> template_params = instantiation_context.template_params;
	std::span<const TemplateTypeArg> template_args = instantiation_context.template_args;
	const FlashCpp::TemplateInstantiationKey& key = instantiation_context.key;
	const uintptr_t overload_id = instantiation_context.overload_id;
	const std::span<const size_t>* template_param_arg_starts = binding_data.template_param_arg_starts;
	const std::span<const size_t>* template_param_arg_counts = binding_data.template_param_arg_counts;
	const std::optional<CallArgDeductionInfo>* deduction_info = binding_data.deduction_info;
	const std::optional<TypeSpecifierNode>* reparsed_trailing_return_type = binding_data.reparsed_trailing_return_type;
	const bool preserve_ref_qualifier =
		hasInstantiationFlag(instantiation_flags, FunctionTemplateInstantiationFlags::PreserveRefQualifier);
	const bool use_explicit_materialization =
		hasInstantiationFlag(instantiation_flags, FunctionTemplateInstantiationFlags::ExplicitMaterialization);
	const bool cacheable_instantiation =
		hasInstantiationFlag(instantiation_flags, FunctionTemplateInstantiationFlags::CacheableInstantiation);
	const bool commit_instantiation =
		hasInstantiationFlag(instantiation_flags, FunctionTemplateInstantiationFlags::CommitInstantiation);
	const bool register_instantiation =
		hasInstantiationFlag(instantiation_flags, FunctionTemplateInstantiationFlags::RegisterInstantiation);
	const bool skip_body_materialization =
		hasInstantiationFlag(instantiation_flags, FunctionTemplateInstantiationFlags::SkipBodyMaterialization);
	const DeclarationNode& orig_decl = func_decl.decl_node();
	std::string_view mangled_name = gTemplateRegistry.mangleTemplateName(template_name, template_args);

	auto apply_resolved_alias_metadata_local = [&](TypeSpecifierNode& type_spec) {
		if (!type_spec.type_index().is_valid()) {
			return;
		}
		const ResolvedAliasTypeInfo alias_info = resolveAliasTypeInfo(type_spec.type_index());
		TypeIndex resolved_alias_index = alias_info.type_index;
		if (!resolved_alias_index.is_valid() && alias_info.typeEnum() != TypeCategory::Invalid) {
			resolved_alias_index = nativeTypeIndex(alias_info.typeEnum());
			if (!resolved_alias_index.is_valid()) {
				resolved_alias_index = TypeIndex{0, alias_info.typeEnum()};
			}
		}
		if (resolved_alias_index.category() != TypeCategory::Invalid &&
			(resolved_alias_index != type_spec.type_index() ||
			 type_spec.category() != alias_info.typeEnum())) {
			type_spec.set_type_index(resolved_alias_index.withCategory(alias_info.typeEnum()));
			type_spec.set_category(alias_info.typeEnum());
		}
		type_spec.add_pointer_levels(static_cast<int>(alias_info.pointer_depth));
		if (type_spec.reference_qualifier() == ReferenceQualifier::None &&
			alias_info.reference_qualifier != ReferenceQualifier::None) {
			type_spec.set_reference_qualifier(alias_info.reference_qualifier);
		}
		if (!type_spec.has_function_signature() && alias_info.function_signature.has_value()) {
			type_spec.set_function_signature(*alias_info.function_signature);
		}
		const int resolved_size_bits = getTypeSpecSizeBits(type_spec);
		if (resolved_size_bits > 0) {
			type_spec.set_size_in_bits(resolved_size_bits);
		}
	};

	auto enterSourceNamespaceIfNeeded = [&]() -> int {
		NamespaceHandle source_namespace = func_decl.namespace_handle();
		if (!source_namespace.isValid() || source_namespace.isGlobal()) {
			return 0;
		}
		InlineVector<NamespaceHandle, 8> chain;
		NamespaceHandle current = source_namespace;
		while (current.isValid() && !current.isGlobal()) {
			chain.push_back(current);
			current = gNamespaceRegistry.getParent(current);
		}
		for (int i = static_cast<int>(chain.size()) - 1; i >= 0; --i) {
			gSymbolTable.enter_namespace(chain[i]);
		}
		return static_cast<int>(chain.size());
	};
	auto exitSourceNamespaceIfNeeded = [&](int entered) {
		for (int i = 0; i < entered; ++i) {
			gSymbolTable.exit_scope();
		}
	};

	ASTNode return_type;
	Token func_name_token = orig_decl.identifier_token();
	if (use_explicit_materialization) {
		Token mangled_token(Token::Type::Identifier, mangled_name,
							orig_decl.identifier_token().line(), orig_decl.identifier_token().column(),
							orig_decl.identifier_token().file_index());
		func_name_token = mangled_token;
		const TypeSpecifierNode& orig_return_type = orig_decl.type_specifier_node();
		if (reparsed_trailing_return_type != nullptr && reparsed_trailing_return_type->has_value()) {
			TypeSpecifierNode reparsed_return_type = **reparsed_trailing_return_type;
			apply_resolved_alias_metadata_local(reparsed_return_type);
			const int resolved_size_bits = getTypeSpecSizeBits(reparsed_return_type);
			if (resolved_size_bits > 0) {
				reparsed_return_type.set_size_in_bits(resolved_size_bits);
			}
			return_type = emplace_node<TypeSpecifierNode>(reparsed_return_type);
		} else if (func_decl.has_template_declaration_position()) {
			SaveHandle current_pos = save_token_position();
			restore_lexer_position_only(func_decl.template_declaration_position());
			FlashCpp::ScopedState guard_param_names(currentTemplateParamState());
			FlashCpp::ScopedState guard_subs(template_param_substitutions_);
			TemplateEnvironment substitution_environment = buildTemplateEnvironment(
				template_params,
				template_args,
				nullptr);
			template_param_substitutions_.clear();
			populateTemplateParamSubstitutions(template_param_substitutions_, substitution_environment);
			for (const TemplateParameterNode& template_param : template_params) {
				pushCurrentTemplateParamName(template_param.nameHandle());
			}
			FlashCpp::TemplateParameterScope template_scope;
			registerTypeParamsInScope(substitution_environment, template_scope, preserve_ref_qualifier);
			const int entered_namespace_count = enterSourceNamespaceIfNeeded();
			auto exit_source_namespace = ScopeGuard([&]() {
				exitSourceNamespaceIfNeeded(entered_namespace_count);
			});
			auto return_type_result = parse_type_specifier();
			if (return_type_result.node().has_value() && return_type_result.node()->is<TypeSpecifierNode>()) {
				auto& rt = return_type_result.node()->as<TypeSpecifierNode>();
				consume_pointer_ref_modifiers(rt);
			}
			restore_lexer_position_only(current_pos);
			if (return_type_result.is_error()) {
				return failTemplateInstantiation(return_type_result.error_message(), &key, overload_id);
			}
			if (!return_type_result.node().has_value()) {
				return failTemplateInstantiation(
					StringBuilder()
						.append("template function '")
						.append(template_name)
						.append("' return type parsing returned no node")
						.commit(),
					&key,
					overload_id);
			}
			return_type = *return_type_result.node();
		} else {
			TypeSpecifierNode substituted_return_type = buildSubstitutedTypeSpecifier(
				orig_return_type,
				orig_decl.type_node(),
				orig_decl.identifier_token(),
				template_params,
				template_args,
				[this](const ASTNode& node, const auto& params, const auto& args) {
					return substituteTemplateParameters(node, params, args);
				},
				[this](const TypeSpecifierNode& type_spec, const auto& params, const auto& args) {
					return substitute_template_parameter(type_spec, params, args);
				},
				nullptr,
				TypeIndex{},
				TypeIndex{},
				false,
				true);
			return_type = emplace_node<TypeSpecifierNode>(substituted_return_type);
		}
	} else {
		const TypeSpecifierNode& orig_return_type = orig_decl.type_specifier_node();
		bool should_reparse = func_decl.has_template_declaration_position();
		if (!should_reparse) {
			if (gTemplateRegistry.lookup_alias_template(orig_return_type.token().handle()).has_value()) {
				should_reparse = true;
			} else if (const TypeInfo* orig_return_type_info =
						   tryGetTypeInfo(orig_return_type.type_index());
					   orig_return_type_info != nullptr &&
					   (orig_return_type_info->isDependentMemberType() ||
						orig_return_type_info->isTemplateInstantiation())) {
				should_reparse = true;
			}
		}
		if (should_reparse) {
			static thread_local std::unordered_set<std::string_view> trailing_return_in_progress;
			if (trailing_return_in_progress.count(mangled_name)) {
				FLASH_LOG(Templates, Debug, "Cycle detected in trailing return type for '", template_name, "' (mangled: '", mangled_name, "'), returning auto to break cycle");
				return std::nullopt;
			}
			trailing_return_in_progress.insert(mangled_name);
			struct TrailingReturnGuard {
				std::unordered_set<std::string_view>& set;
				std::string_view key;
				~TrailingReturnGuard() { set.erase(key); }
			} trailing_return_guard{trailing_return_in_progress, mangled_name};
			SaveHandle current_pos = save_token_position();
			restore_lexer_position_only(func_decl.template_declaration_position());
			FlashCpp::ScopedState guard_param_names(currentTemplateParamState());
			FlashCpp::ScopedState guard_subs(template_param_substitutions_);
			TemplateEnvironment substitution_environment = buildTemplateEnvironment(
				template_params,
				template_args,
				nullptr);
			template_param_substitutions_.clear();
			populateTemplateParamSubstitutions(template_param_substitutions_, substitution_environment);
			for (const TemplateParameterNode& template_param : template_params) {
				pushCurrentTemplateParamName(template_param.nameHandle());
			}
			FlashCpp::TemplateParameterScope template_scope;
			registerTypeParamsInScope(substitution_environment, template_scope, false);
			const int entered_namespace_count = enterSourceNamespaceIfNeeded();
			auto exit_return_type_namespace = ScopeGuard([&]() {
				exitSourceNamespaceIfNeeded(entered_namespace_count);
			});
			auto return_type_result = parse_type_specifier();
			if (return_type_result.node().has_value() && return_type_result.node()->is<TypeSpecifierNode>()) {
				auto& rt = return_type_result.node()->as<TypeSpecifierNode>();
				consume_pointer_ref_modifiers(rt);
			}
			restore_lexer_position_only(current_pos);
			if (return_type_result.is_error()) {
				return failTemplateInstantiation(return_type_result.error_message(), &key, overload_id);
			}
			if (!return_type_result.node().has_value()) {
				return failTemplateInstantiation(
					StringBuilder()
						.append("template function '")
						.append(template_name)
						.append("' return type parsing returned no node")
						.commit(),
					&key,
					overload_id);
			}
			return_type = *return_type_result.node();
			restore_lexer_position_only(func_decl.template_declaration_position());
			FlashCpp::TemplateParameterScope template_scope2;
			registerTypeParamsInScope(substitution_environment, template_scope2, false);
			const int entered_name_parse_namespace_count = enterSourceNamespaceIfNeeded();
			auto exit_name_parse_namespace = ScopeGuard([&]() {
				exitSourceNamespaceIfNeeded(entered_name_parse_namespace_count);
			});
			auto type_and_name_result = parse_type_and_name();
			restore_lexer_position_only(current_pos);
			if (!type_and_name_result.is_error() &&
				type_and_name_result.node().has_value() &&
				type_and_name_result.node()->is<DeclarationNode>()) {
				func_name_token = type_and_name_result.node()->as<DeclarationNode>().identifier_token();
			}
		} else {
			TypeIndex return_type_index = substitute_template_parameter(
				orig_return_type, template_params, template_args);
			TypeSpecifierNode new_return_type(
				return_type_index.category(),
				TypeQualifier::None,
				get_type_size_bits(return_type_index.category()),
				Token(),
				orig_return_type.cv_qualifier());
			new_return_type.set_type_index(return_type_index);
			new_return_type.set_reference_qualifier(orig_return_type.reference_qualifier());
			propagateFunctionSignatureFromTemplateArg(
				new_return_type,
				orig_return_type,
				return_type_index,
				template_params,
				template_args);
			applyTemplateArgIndirection(new_return_type, orig_return_type, template_params, template_args,
										/*propagate_reference_qualifier=*/false);
			for (const auto& ptr_level : orig_return_type.pointer_levels()) {
				new_return_type.add_pointer_level(ptr_level.cv_qualifier);
			}
			apply_resolved_alias_metadata_local(new_return_type);
			return_type = emplace_node<TypeSpecifierNode>(new_return_type);
		}
	}

	resolveDependentMemberAlias(return_type, template_params, template_args);
	if (return_type.is<TypeSpecifierNode>()) {
		auto& rt = return_type.as<TypeSpecifierNode>();
		resolveAliasTemplateInstantiation(rt);
		apply_resolved_alias_metadata_local(rt);
		if ((rt.category() == TypeCategory::UserDefined ||
			 rt.category() == TypeCategory::TypeAlias ||
			 rt.category() == TypeCategory::Template) &&
			rt.type_index().is_valid()) {
			if (const TypeInfo* rt_info = tryGetTypeInfo(rt.type_index())) {
				if (rt_info->is_incomplete_instantiation_ && rt_info->isDependentMemberType()) {
					gTemplateRegistry.markFailedInstantiation(key, overload_id);
					return std::nullopt;
				}
			}
		}
	}

	auto new_decl = emplace_node<DeclarationNode>(return_type, func_name_token);
	auto [new_func_node, new_func_ref] = emplace_node_ref<FunctionDeclarationNode>(new_decl.as<DeclarationNode>());

	auto saved_outer_pack_param_info = std::move(pack_param_info_);
	pack_param_info_.clear();
	ScopeGuard restore_outer_pack_param_info([&]() {
		pack_param_info_ = std::move(saved_outer_pack_param_info);
	});

	if (!materializeTemplateFunctionParameters(
			new_func_ref,
			instantiation_context,
			binding_data,
			instantiation_flags)) {
		return std::nullopt;
	}

	auto saved_template_pack_sizes = std::move(template_param_pack_sizes_);
	ScopeGuard restore_template_pack_sizes([&]() {
		template_param_pack_sizes_ = std::move(saved_template_pack_sizes);
	});
	template_param_pack_sizes_.clear();
	if (use_explicit_materialization) {
		if (template_param_arg_starts == nullptr || template_param_arg_counts == nullptr) {
			return std::nullopt;
		}
		for (size_t pi = 0; pi < template_params.size(); ++pi) {
			const TemplateParameterNode* tparam_ptr = tryGetTemplateParameterNode(template_params[pi]);
			if (tparam_ptr == nullptr) {
				continue;
			}
			const auto& tparam = *tparam_ptr;
			if (tparam.is_variadic() && (*template_param_arg_starts)[pi] != SIZE_MAX) {
				template_param_pack_sizes_.emplace_back(tparam.nameHandle(), (*template_param_arg_counts)[pi]);
			}
		}
	} else if (deduction_info != nullptr && deduction_info->has_value()) {
		const size_t func_pack_call_arg_count =
			((*deduction_info)->function_pack_call_arg_end != SIZE_MAX &&
			 (*deduction_info)->function_pack_call_arg_start != SIZE_MAX &&
			 (*deduction_info)->function_pack_call_arg_end >= (*deduction_info)->function_pack_call_arg_start)
				? ((*deduction_info)->function_pack_call_arg_end - (*deduction_info)->function_pack_call_arg_start)
				: 0;
		for (const auto& tpnode : template_params) {
			const TemplateParameterNode* tparam_ptr = tryGetTemplateParameterNode(tpnode);
			if (tparam_ptr == nullptr || !tparam_ptr->is_variadic()) {
				continue;
			}
			const size_t pack_count =
				(*deduction_info)->function_pack_dependent_param_names.count(tparam_ptr->nameHandle())
					? func_pack_call_arg_count
					: 0;
			template_param_pack_sizes_.emplace_back(tparam_ptr->nameHandle(), pack_count);
		}
	}

	bool body_reparse_failed = false;
	if (skip_body_materialization) {
		// Shape-only overload selection needs a concrete function signature for
		// conversion ranking, but it must not parse or substitute the body of
		// candidates that may not be selected.
	} else if (func_decl.has_template_body_position()) {
		if (use_explicit_materialization) {
			static thread_local std::unordered_set<StringHandle> body_parse_in_progress;
			StringHandle cycle_key = StringTable::getOrInternStringHandle(mangled_name);
			if (body_parse_in_progress.count(cycle_key)) {
				FLASH_LOG(Templates, Debug, "Cycle detected in function template body parsing for '", template_name, "' (mangled: '", mangled_name, "'), skipping body");
				return std::nullopt;
			}
			body_parse_in_progress.insert(cycle_key);
			struct BodyParseGuard {
				std::unordered_set<StringHandle>& set;
				StringHandle key;
				~BodyParseGuard() { set.erase(key); }
			} body_guard{body_parse_in_progress, cycle_key};
			bool saved_has_parameter_packs = has_parameter_packs_;
			ScopeGuard restore_has_parameter_packs([&]() {
				has_parameter_packs_ = saved_has_parameter_packs;
			});
			if (!pack_param_info_.empty()) {
				has_parameter_packs_ = true;
			}
			reparse_template_function_body(new_func_ref, func_decl, template_params, template_args, preserve_ref_qualifier);
		} else {
			static thread_local std::unordered_set<std::string_view> body_reparse_in_progress;
			std::string_view cycle_key = mangled_name;
			if (body_reparse_in_progress.count(cycle_key)) {
				return ASTNode(&new_func_ref);
			}
			body_reparse_in_progress.insert(cycle_key);
			struct BodyReparseGuard {
				std::unordered_set<std::string_view>& set;
				std::string_view key;
				~BodyReparseGuard() { set.erase(key); }
			} body_reparse_guard{body_reparse_in_progress, cycle_key};
			bool saved_has_parameter_packs = has_parameter_packs_;
			ScopeGuard restore_has_parameter_packs([&]() {
				has_parameter_packs_ = saved_has_parameter_packs;
			});
			if (!pack_param_info_.empty()) {
				has_parameter_packs_ = true;
			}
			if (template_instantiation_mode_ == TemplateInstantiationMode::HardUseCandidateProbe) {
				ScopedParserInstantiationContext body_instantiation_mode(*this, TemplateInstantiationMode::HardUse, StringHandle{});
				reparse_template_function_body(new_func_ref, func_decl, template_params, template_args,
											   preserve_ref_qualifier);
			} else {
				reparse_template_function_body(new_func_ref, func_decl, template_params, template_args,
											   preserve_ref_qualifier);
			}
			if (!new_func_ref.is_materialized()) {
				StringBuilder reason_builder;
				StringHandle body_reparse_failure_reason = StringTable::getOrInternStringHandle(
					reason_builder
						.append("failed to reparse template function body for ")
						.append(mangled_name)
						.commit());
				new_func_ref.mark_failed_substitution(body_reparse_failure_reason);
				failTemplateInstantiation(
					StringTable::getStringView(body_reparse_failure_reason),
					&key,
					overload_id);
				body_reparse_failed = true;
			}
		}
	} else {
		auto orig_body = func_decl.get_definition();
		if (orig_body.has_value()) {
			if (use_explicit_materialization) {
				new_func_ref.set_definition(
					substituteTemplateParameters(*orig_body, template_params, template_args));
			} else {
				throw InternalError("Template function definition has no saved body position");
			}
		}
	}

	if (body_reparse_failed) {
		if (cacheable_instantiation && commit_instantiation && register_instantiation) {
			gTemplateRegistry.registerInstantiation(key, new_func_node);
		}
		return std::nullopt;
	}

	return finalizeInstantiatedFunction(
		new_func_node,
		new_func_ref,
		instantiation_context,
		mangled_name,
		instantiation_flags);
}

TemplateNameLookupRequest Parser::buildTemplateNameLookupRequest(
	StringHandle template_name,
	TemplateNameLookupKind lookup_kind,
	bool is_dependent) const {
	TemplateNameLookupRequest request;
	request.name = template_name;
	request.lookup_kind = lookup_kind;
	request.is_dependent = is_dependent;
	request.timing = TemplateNameLookupTiming::PointOfInstantiation;
	request.point_of_instantiation_namespace = gSymbolTable.get_current_namespace_handle();
	request.definition_namespace = request.point_of_instantiation_namespace;
	if (current_template_definition_lookup_context_ != nullptr &&
		current_template_definition_lookup_context_->is_valid()) {
		request.timing = TemplateNameLookupTiming::Immediate;
		request.definition_namespace =
			current_template_definition_lookup_context_->definition_namespace;
		request.current_instantiation_name =
			current_template_definition_lookup_context_->current_instantiation_name;
	}
	return request;
}

TemplateNameLookupRequest Parser::buildFunctionTemplateLookupRequest(
	StringHandle template_name,
	TemplateNameLookupKind lookup_kind,
	bool is_dependent) const {
	return buildTemplateNameLookupRequest(
		template_name,
		lookup_kind,
		is_dependent);
}

std::vector<ASTNode> Parser::materializeFunctionTemplateCandidateDeclarations(
	std::span<const TemplateNameLookupCandidate> candidates) const {
	std::vector<ASTNode> declarations;
	declarations.reserve(candidates.size());
	for (const TemplateNameLookupCandidate& candidate : candidates) {
		declarations.push_back(candidate.declaration);
	}
	return declarations;
}

std::vector<TemplateNameLookupCandidate> Parser::lookupFunctionTemplateCandidatesForInstantiation(
	std::string_view template_name,
	int recursion_depth) {
	std::vector<TemplateNameLookupCandidate> candidates;
	std::unordered_set<const void*> seen_declarations;
	const StringHandle template_name_handle =
		StringTable::getOrInternStringHandle(template_name);
	const bool name_is_qualified = template_name.find("::") != std::string_view::npos;

	auto append_candidates = [&](const TemplateNameLookupResult& lookup_result) {
		for (const TemplateNameLookupCandidate& candidate : lookup_result.candidates) {
			if (candidate.identity.kind != TemplateDeclarationKind::FunctionTemplate) {
				continue;
			}
			const void* declaration_address = candidate.declaration.raw_pointer();
			if (declaration_address == nullptr) {
				continue;
			}
			if (!seen_declarations.insert(declaration_address).second) {
				continue;
			}
			candidates.push_back(candidate);
		}
	};

	TemplateNameLookupRequest primary_request = buildFunctionTemplateLookupRequest(
		template_name_handle,
		name_is_qualified ? TemplateNameLookupKind::Qualified : TemplateNameLookupKind::Ordinary,
		false);
	append_candidates(gTemplateRegistry.lookupTemplateName(primary_request));

	if (candidates.empty() && !name_is_qualified) {
		NamespaceHandle current_handle = gSymbolTable.get_current_namespace_handle();
		while (!current_handle.isGlobal() && candidates.empty()) {
			StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(
				current_handle,
				template_name_handle);
			TemplateNameLookupRequest qualified_request = buildFunctionTemplateLookupRequest(
				qualified_handle,
				TemplateNameLookupKind::Qualified,
				false);
			append_candidates(gTemplateRegistry.lookupTemplateName(qualified_request));
			if (candidates.empty()) {
				FLASH_LOG_FORMAT(
					Templates,
					Debug,
					"[depth={}]: Template '{}' not found, trying qualified name '{}'",
					recursion_depth,
					template_name,
					StringTable::getStringView(qualified_handle));
			}
			current_handle = gNamespaceRegistry.getParent(current_handle);
		}
	}

	if (candidates.empty() && !struct_parsing_context_stack_.empty()) {
		const auto& current_struct_context = struct_parsing_context_stack_.back();
		StringHandle current_struct_name =
			StringTable::getOrInternStringHandle(current_struct_context.struct_name);
		FLASH_LOG_FORMAT(
			Templates,
			Debug,
			"[depth={}]: Template '{}' not found, checking inherited templates from struct '{}'",
			recursion_depth,
			template_name,
			current_struct_context.struct_name);
		const std::vector<ASTNode>* inherited_templates =
			lookup_inherited_template(current_struct_name, template_name);
		if (inherited_templates != nullptr) {
			size_t overload_ordinal = 0;
			for (const ASTNode& inherited_node : *inherited_templates) {
				if (!inherited_node.is<TemplateFunctionDeclarationNode>()) {
					continue;
				}
				const void* declaration_address = inherited_node.raw_pointer();
				if (declaration_address == nullptr ||
					!seen_declarations.insert(declaration_address).second) {
					continue;
				}
				TemplateNameLookupCandidate candidate;
				candidate.declaration = inherited_node;
				candidate.identity.kind = TemplateDeclarationKind::FunctionTemplate;
				candidate.identity.lookup_name = template_name_handle;
				candidate.identity.declared_name =
					inherited_node
						.as<TemplateFunctionDeclarationNode>()
						.function_decl_node()
						.decl_node()
						.identifier_token()
						.handle();
				candidate.identity.declaration_address = declaration_address;
				candidate.identity.overload_ordinal = overload_ordinal++;
				candidates.push_back(candidate);
			}
		}
	}

	if (!candidates.empty() && !name_is_qualified) {
		std::vector<ASTNode> declarations =
			materializeFunctionTemplateCandidateDeclarations(candidates);
		filterPhase1OrdinaryFunctionOverloads(declarations);
		if (declarations.size() != candidates.size()) {
			std::unordered_set<const void*> kept_declarations;
			kept_declarations.reserve(declarations.size());
			for (const ASTNode& declaration : declarations) {
				kept_declarations.insert(declaration.raw_pointer());
			}
			candidates.erase(
				std::remove_if(
					candidates.begin(),
					candidates.end(),
					[&](const TemplateNameLookupCandidate& candidate) {
						return kept_declarations.count(
								   candidate.declaration.raw_pointer()) == 0;
					}),
				candidates.end());
		}
	}

	return candidates;
}

std::optional<ASTNode> Parser::try_instantiate_template_explicit(std::string_view template_name, std::span<const TemplateTypeArg> explicit_types, size_t call_arg_count) {
	static int recursion_depth = 0;
	recursion_depth++;
	struct DepthGuard { int& d; ~DepthGuard() { d--; } } depth_guard{recursion_depth};
	for (const TemplateTypeArg& arg : explicit_types) {
		if (arg.is_dependent || arg.dependent_name.isValid()) {
			return std::nullopt;
		}
	}
	// FIRST: Check if we have an explicit specialization for these template arguments
	// This handles cases like: template<> int sum<int, int>(int, int) being called as sum<int, int>(3, 7)
	auto specialization_opt = gTemplateRegistry.lookupSpecialization(template_name, explicit_types);
	if (specialization_opt.has_value()) {
		FLASH_LOG(Templates, Debug, "Found explicit specialization for ", template_name);
		return *specialization_opt;
	}

	std::vector<ASTNode> all_templates =
		materializeFunctionTemplateCandidateDeclarations(
			lookupFunctionTemplateCandidatesForInstantiation(template_name, recursion_depth));
	if (all_templates.empty()) {
		return std::nullopt;	 // No template with this name
	}
	const bool outer_sfinae_context =
		template_instantiation_mode_ == TemplateInstantiationMode::SoftProbe;
	std::vector<size_t> overload_iteration_order;
	overload_iteration_order.resize(all_templates.size());
	std::iota(overload_iteration_order.begin(), overload_iteration_order.end(), size_t{0});
	if (!outer_sfinae_context) {
		constexpr int kNonFunctionTemplateScore = -1;
		std::vector<int> scores;
		scores.reserve(all_templates.size());
		for (const auto& node : all_templates) {
			scores.push_back(node.is<TemplateFunctionDeclarationNode>()
				? computeTemplateFunctionSpecificity(node.as<TemplateFunctionDeclarationNode>())
				: kNonFunctionTemplateScore);
		}
		std::stable_sort(
			overload_iteration_order.begin(),
			overload_iteration_order.end(),
			[&](size_t lhs_idx, size_t rhs_idx) {
				return scores[lhs_idx] > scores[rhs_idx];
			});
	}

	struct ExplicitOverloadCandidate {
		size_t overload_idx = 0;
		const TemplateFunctionDeclarationNode* template_func = nullptr;
		InlineVector<TemplateTypeArg, 4> template_args;
		std::vector<size_t> template_param_arg_starts;
		std::vector<size_t> template_param_arg_counts;
		std::optional<CallArgDeductionInfo> deduction_info;
		std::optional<TypeSpecifierNode> reparsed_trailing_return_type;
		FlashCpp::TemplateInstantiationKey key;
		uintptr_t overload_id = 0;
	};
	std::vector<ExplicitOverloadCandidate> viable_candidates;
	viable_candidates.reserve(all_templates.size());
	const bool use_shape_preselection =
		!outer_sfinae_context &&
		current_explicit_call_arg_types_ != nullptr &&
		all_templates.size() > 1;
	const bool use_overload_discriminated_key = all_templates.size() > 1;

	// Loop over all overloads for SFINAE support
	for (size_t sorted_idx = 0; sorted_idx < overload_iteration_order.size(); ++sorted_idx) {
		size_t overload_idx = overload_iteration_order[sorted_idx];
		const ASTNode& template_node = all_templates[overload_idx];
		if (!template_node.is<TemplateFunctionDeclarationNode>()) {
			continue;  // Not a function template, try next overload
		}

		const TemplateFunctionDeclarationNode& template_func = template_node.as<TemplateFunctionDeclarationNode>();
		const auto& template_params = template_func.template_parameters();
		const FunctionDeclarationNode& func_decl = template_func.function_decl_node();
		FLASH_LOG_FORMAT(Templates, Debug, "[explicit] func_decl name='{}' ns={}",
			func_decl.decl_node().identifier_token().value(),
			func_decl.namespace_handle().isValid()
				? gNamespaceRegistry.getQualifiedName(func_decl.namespace_handle())
				: "(invalid)");
		if (call_arg_count != SIZE_MAX &&
			!functionTemplateAcceptsCallArgumentCount(template_params, func_decl, call_arg_count)) {
			continue;
		}

		// Check if template has a variadic parameter pack
		bool has_variadic_pack = false;
		for (const auto& param : template_params) {
			if (param.is_variadic()) {
				has_variadic_pack = true;
				break;
			}
		}

		// Verify we have the right number of template arguments.
		size_t required_template_args = countRequiredTemplateArgsAfter(
			template_params, 0);
		size_t max_template_args = template_params.size();
		const bool can_deduce_remaining_explicit_args = current_explicit_call_arg_types_ != nullptr;
		if (!has_variadic_pack) {
			if ((!can_deduce_remaining_explicit_args && explicit_types.size() < required_template_args) ||
				explicit_types.size() > max_template_args) {
				continue;
			}
		} else if (!can_deduce_remaining_explicit_args && explicit_types.size() < required_template_args) {
			continue;
		}

		// Build template argument list
		InlineVector<TemplateTypeArg, 4> template_args;
		std::vector<size_t> template_param_arg_starts(template_params.size(), SIZE_MAX);
		std::vector<size_t> template_param_arg_counts(template_params.size(), 0);
		size_t explicit_idx = 0;	 // Track position in explicit_types
		std::unordered_map<StringHandle, TemplateTypeArg, StringHash, StringEqual> param_name_to_arg;
		std::optional<CallArgDeductionInfo> deduction_info;
		// Build a name-to-arg deduction map whenever call arg types are available,
		// regardless of whether the template has variadic parameters.
		// buildDeductionMapFromCallArgs now safely skips parameter-pack function slots,
		// so non-pack template params (T, U in template<T, U, ...Rest>) are pre-deduced
		// from the corresponding call argument positions.
		if (current_explicit_call_arg_types_ != nullptr) {
			// C++20 [temp.arg.explicit]/3: explicitly specified template arguments
			// are substituted before any remaining deduction is performed.  Seed the
			// deduction map with those bindings so call-argument pre-deduction does
			// not incorrectly reject an already-bound parameter pattern such as U*
			// when the call argument is a null pointer constant.
			std::unordered_map<StringHandle, TemplateTypeArg, StringHash, StringEqual> explicitly_bound_args;
			size_t explicit_seed_idx = 0;
			for (size_t param_idx = 0;
				 param_idx < template_params.size() && explicit_seed_idx < explicit_types.size();
				 ++param_idx) {
				const TemplateParameterNode& param = template_params[param_idx];
				if (param.is_variadic()) {
					size_t remaining_args = explicit_types.size() - explicit_seed_idx;
					explicit_seed_idx += remaining_args;
					continue;
				}
				explicitly_bound_args.emplace(param.nameHandle(), explicit_types[explicit_seed_idx]);
				++explicit_seed_idx;
			}
			deduction_info = buildDeductionMapFromCallArgs(
				template_params,
				func_decl,
				*current_explicit_call_arg_types_,
				recursion_depth,
				&explicitly_bound_args);
			if (!deduction_info.has_value()) {
				continue;
			}
			param_name_to_arg = std::move(deduction_info->param_name_to_arg);
		}
		bool overload_mismatch = false;
		const auto recordTemplateParamArgRange = [&](size_t param_index, size_t arg_start_index) {
			template_param_arg_starts[param_index] = arg_start_index;
			template_param_arg_counts[param_index] = template_args.size() - arg_start_index;
		};
		for (size_t i = 0; i < template_params.size(); ++i) {
			const TemplateParameterNode& param = template_params[i];
			size_t arg_start_index = template_args.size();
			if (param.kind() == TemplateParameterKind::Template) {
				if (explicit_idx < explicit_types.size()) {
					StringHandle tpl_name_handle;
					const auto& arg = explicit_types[explicit_idx];
					if (arg.is_template_template_arg && arg.template_name_handle.isValid()) {
						tpl_name_handle = arg.template_name_handle;
					} else if (arg.category() == TypeCategory::Struct) {
						if (const TypeInfo* type_info = tryGetTypeInfo(arg.type_index))
							tpl_name_handle = type_info->name();
					} else if (arg.is_dependent) {
						tpl_name_handle = arg.dependent_name;
					}
					template_args.push_back(TemplateTypeArg::makeTemplate(tpl_name_handle));
					++explicit_idx;
				} else if (!tryAppendDefaultTemplateArg(param, template_params, template_args, func_decl.namespace_handle())) {
					overload_mismatch = true;
					break;
				}
				recordTemplateParamArgRange(i, arg_start_index);
			} else if (param.is_variadic()) {
				size_t remaining_args = explicit_idx < explicit_types.size()
										   ? explicit_types.size() - explicit_idx
										   : 0;
				size_t pack_size = remaining_args;
				if (current_explicit_call_arg_types_ == nullptr) {
					size_t required_after = countRequiredTemplateArgsAfter(
						template_params, i + 1);
					pack_size = remaining_args > required_after
									 ? remaining_args - required_after
									 : 0;
				}
				for (size_t j = 0; j < pack_size; ++j) {
					template_args.push_back(explicit_types[explicit_idx + j]);
				}
				explicit_idx += pack_size;
				// Pack-aware explicit deduction (Phase 6): explicit args can seed the
				// front of the template-parameter pack and the remainder is deduced from
				// the mapped function-parameter-pack call-arg slice.
				// This handles e.g. count_rest<int>(1, 2, 3) → T=int explicit,
				// Rest deduced as {int,int} from call args 1 and 2, and mixed
				// explicit+deduced pack cases such as pack<int>(1, 2.0).
				// NOTE: current_explicit_call_arg_types_ may be null here — the
				// deduction-map build block (lines ~980-990) is conditional and does
				// not guarantee non-null for this later point in the same function.
				// IMPORTANT: only apply this block for template-parameter packs that
				// participate in the function-parameter pack expansion.  A template may
				// have multiple packs (e.g. template<int... Ns, typename... Ts>), where
				// only Ts expands in the function signature.  The guard uses
				// function_pack_dependent_param_names, which covers both the primary
				// pack (Ts) and co-packs (Us in "Pair<Ts,Us>...").
				if (current_explicit_call_arg_types_ != nullptr &&
					deduction_info.has_value() &&
					deduction_info->function_pack_call_arg_start != SIZE_MAX &&
					deduction_info->function_pack_dependent_param_names.count(param.nameHandle())) {
					const size_t pack_call_arg_count =
						deduction_info->function_pack_call_arg_end > deduction_info->function_pack_call_arg_start
							? deduction_info->function_pack_call_arg_end - deduction_info->function_pack_call_arg_start
							: 0;
					// Overload-mismatch check only for the primary pack (the one whose
					// name matches function_pack_template_param_name).  Co-packs always
					// end up with the same count as the primary pack.
					if (deduction_info->function_pack_template_param_name == param.nameHandle() &&
						pack_size > pack_call_arg_count) {
						overload_mismatch = true;
						break;
					}
					for (size_t j = deduction_info->function_pack_call_arg_start + pack_size;
						 j < deduction_info->function_pack_call_arg_end &&
						 j < current_explicit_call_arg_types_->size();
						 ++j) {
						const TypeSpecifierNode& call_arg = (*current_explicit_call_arg_types_)[j];
						bool pushed = false;
						if (auto extracted_arg = extractNestedTemplateArgForDependentName(
								deduction_info->function_pack_element_type_index,
								call_arg.type_index(),
								param.nameHandle())) {
							template_args.push_back(*extracted_arg);
							pushed = true;
						}
						if (!pushed) {
							template_args.push_back(TemplateTypeArg::makeTypeSpecifier(call_arg));
						}
					}
				}
				recordTemplateParamArgRange(i, arg_start_index);
			} else {
				if (explicit_idx < explicit_types.size()) {
					template_args.push_back(explicit_types[explicit_idx]);
					++explicit_idx;
				} else {
					// No explicit arg left — try name-based deduction (pre-deduced map)
					// first, then positional fallback for trailing params after a pack,
					// then default, then overload mismatch.
					StringHandle param_handle = param.nameHandle();
					auto map_it = param_name_to_arg.find(param_handle);
					if (map_it != param_name_to_arg.end()) {
						template_args.push_back(map_it->second);
						recordTemplateParamArgRange(i, arg_start_index);
						continue;
					}
					if (tryAppendDefaultTemplateArg(param, template_params, template_args, func_decl.namespace_handle())) {
						recordTemplateParamArgRange(i, arg_start_index);
						continue;
					}

					// No explicit arg, no call arg to deduce from, and no usable default.
					FLASH_LOG_FORMAT(Templates, Debug, "Template overload mismatch: need argument at position {} but only {} types provided",
									 explicit_idx, explicit_types.size());
					overload_mismatch = true;
					break;
				}
				recordTemplateParamArgRange(i, arg_start_index);
			}
		}
		if (overload_mismatch)
			continue;  // SFINAE: try next overload

		const auto has_structurally_dependent_template_args = [](std::span<const TemplateTypeArg> args) {
			return std::any_of(
				args.begin(),
				args.end(),
				[](const TemplateTypeArg& arg) {
					return arg.is_dependent ||
						   arg.dependent_name.isValid() ||
						   arg.category() == TypeCategory::Auto ||
						   arg.category() == TypeCategory::DeclTypeAuto;
				});
		};
		if (has_structurally_dependent_template_args(template_args)) {
			continue;
		}

		// CHECK REQUIRES CLAUSE CONSTRAINT BEFORE INSTANTIATION
		if (template_func.has_requires_clause()) {
			const RequiresClauseNode& requires_clause =
				template_func.requires_clause()->as<RequiresClauseNode>();

			// Get template parameter names for evaluation
			InlineVector<std::string_view, 4> eval_param_names;
			for (const auto& tparam_node : template_params) {
				eval_param_names.push_back(tparam_node.name());
			}

			// Create a copy of explicit_types with template template arg flags properly set
			InlineVector<TemplateTypeArg, 4> constraint_eval_args = template_args;

			FLASH_LOG(Templates, Debug, "  Evaluating constraint with ", constraint_eval_args.size(), " template args and ", eval_param_names.size(), " param names");

			// Evaluate the constraint with the template arguments
			auto constraint_result = evaluateConstraint(
				requires_clause.constraint_expr(), constraint_eval_args, eval_param_names);

			FLASH_LOG(Templates, Debug, "  Constraint evaluation result: satisfied=", constraint_result.satisfied);

			if (!constraint_result.satisfied) {
				// Constraint not satisfied - report detailed error
				std::string args_str;
				for (size_t j = 0; j < constraint_eval_args.size(); ++j) {
					if (j > 0)
						args_str += ", ";
					args_str += constraint_eval_args[j].toString();
				}

				FLASH_LOG(Parser, Error, "constraint not satisfied for template function '", template_name, "'");
				FLASH_LOG(Parser, Error, "  ", constraint_result.error_message);
				if (!constraint_result.failed_requirement.empty()) {
					FLASH_LOG(Parser, Error, "  failed requirement: ", constraint_result.failed_requirement);
				}
				if (!constraint_result.suggestion.empty()) {
					FLASH_LOG(Parser, Error, "  suggestion: ", constraint_result.suggestion);
				}
				FLASH_LOG(Parser, Error, "  template arguments: ", args_str);

				// Don't create instantiation - constraint failed, try next overload
				continue;
			}
		}

		// CHECK CONCEPT CONSTRAINTS ON TEMPLATE PARAMETERS (C++20 abbreviated templates)
		// For parameters like `template<IsInt _T0>` (from `IsInt auto x`), evaluate the concept
		{
			bool concept_failed = false;
			forEachNonPackTemplateParamArgBinding(
				template_params,
				template_args,
				[&](const TemplateParameterNode& param, const TemplateTypeArg& bound_arg, size_t) {
					if (!param.has_concept_constraint() || overload_mismatch || concept_failed)
						return;
					std::string_view concept_name = param.concept_constraint();
					auto concept_opt = gConceptRegistry.lookupConcept(concept_name);
					if (!concept_opt.has_value())
						return;
					const auto& concept_node = concept_opt->as<ConceptDeclarationNode>();
					TemplateTypeArg concept_arg = bound_arg;
					concept_arg.ref_qualifier = ReferenceQualifier::None;
					InlineVector<TemplateTypeArg, 4> concept_args;
					concept_args.push_back(concept_arg);
					auto constraint_result = evaluateConstraint(
						concept_node, concept_args);
					if (!constraint_result.satisfied) {
						FLASH_LOG(Parser, Error, "concept constraint '", concept_name, "' not satisfied for parameter '", param.name(), "' of '", template_name, "'");
						FLASH_LOG(Parser, Error, "  ", constraint_result.error_message);
						overload_mismatch = true;
						concept_failed = true;
					}
				});
			if (overload_mismatch || concept_failed)
				continue;
		}
		if (overload_mismatch)
			continue;  // SFINAE: concept constraint failed, try next overload

		// SFINAE for trailing return type: if the function has a declaration position for re-parsing,
		// always re-parse the return type with substituted template parameters.
		// During template parsing, trailing return types like decltype(u->foo(), void(), true)
		// may resolve to concrete types (e.g., bool) even when they contain dependent expressions.
		// The re-parse with concrete template arguments will fail if substitution is invalid.
		std::optional<TypeSpecifierNode> reparsed_trailing_return_type;
		if (func_decl.has_trailing_return_type_position()) {
			FlashCpp::ScopedState guard_ptb(parsing_template_depth_);
			FlashCpp::ScopedState guard_param_names(currentTemplateParamState());
			FlashCpp::ScopedState guard_sfinae_map(sfinae_type_map_);
			ScopedParserInstantiationContext guard_instantiation_mode(*this, TemplateInstantiationMode::SoftProbe, StringHandle{});
			parsing_template_depth_ = 0;	 // suppress template body context during SFINAE
			clearCurrentTemplateParameters();  // No dependent names during SFINAE
			sfinae_type_map_.clear();

			SaveHandle sfinae_pos = save_token_position();
			restore_lexer_position_only(func_decl.trailing_return_type_position());
			advance();  // consume '->'

			FlashCpp::TemplateParameterScope sfinae_scope;
			registerTypeParamsInScope(template_params, template_args, sfinae_scope, &sfinae_type_map_);

			// Register function parameters so they're visible in decltype expressions.
			// Materialize substituted parameter types first so expressions like
			// decltype(u->foo(), true) see `u` as `HasFoo*` (or concrete equivalent),
			// not as an unresolved template-parameter placeholder.
			FlashCpp::SymbolTableScope sfinae_param_scope(ScopeType::Function);
			InlineVector<ASTNode, 8> sfinae_params;
			sfinae_params.reserve(func_decl.parameter_nodes().size());
			for (const ASTNode& param_node : func_decl.parameter_nodes()) {
				if (!param_node.is<DeclarationNode>()) {
					sfinae_params.push_back(param_node);
					continue;
				}

				const DeclarationNode& original_param = param_node.as<DeclarationNode>();
				TypeSpecifierNode substituted_type = original_param.type_specifier_node();
				TypeIndex substituted_index = substitute_template_parameter(
					original_param.type_specifier_node(),
					template_params,
					template_args);
				if (substituted_index.is_valid() &&
					substituted_index.category() != TypeCategory::Invalid) {
					substituted_type.set_type_index(substituted_index.withCategory(substituted_index.category()));
					substituted_type.set_category(substituted_index.category());
				}

				ASTNode substituted_type_node = emplace_node<TypeSpecifierNode>(substituted_type);
				ASTNode substituted_param = emplace_node<DeclarationNode>(
					substituted_type_node,
					original_param.identifier_token());
				sfinae_params.push_back(substituted_param);
			}
			register_parameters_in_scope(sfinae_params);

			auto return_type_result = parse_type_specifier();
			// Explicitly exit the param scope before restoring the lexer position.
			gSymbolTable.exit_scope();
			sfinae_param_scope.dismiss();  // prevent double exit in destructor
			restore_lexer_position_only(sfinae_pos);
			// guard_ptb, guard_param_names and guard_sfinae_map restore their fields automatically

			if (return_type_result.is_error() ||
				!return_type_result.node().has_value() ||
				!return_type_result.node()->is<TypeSpecifierNode>()) {
				FLASH_LOG_FORMAT(Templates, Debug, "SFINAE: trailing return type re-parse failed for '{}', trying next overload", template_name);
				continue;  // SFINAE: this overload's return type failed, try next
			}
			reparsed_trailing_return_type = return_type_result.node()->as<TypeSpecifierNode>();
		}

		StringHandle key_name_handle = StringTable::getOrInternStringHandle(template_name);
		if (use_overload_discriminated_key) {
			StringBuilder discriminated_name_builder;
			key_name_handle = StringTable::getOrInternStringHandle(
				discriminated_name_builder
					.append(template_name)
					.append("$ol")
					.append(static_cast<uint64_t>(overload_idx))
					.commit());
		}
		auto key = FlashCpp::makeInstantiationKey(key_name_handle, template_args);
		if (!use_shape_preselection) {
			auto existing_inst = gTemplateRegistry.getInstantiation(key);
			if (existing_inst.has_value()) {
				return *existing_inst;
			}
		}

		viable_candidates.push_back(ExplicitOverloadCandidate{
			overload_idx,
			&template_func,
			std::move(template_args),
			std::move(template_param_arg_starts),
			std::move(template_param_arg_counts),
			std::move(deduction_info),
			std::move(reparsed_trailing_return_type),
			std::move(key),
			reinterpret_cast<uintptr_t>(&func_decl)});
	} // end of overload loop

	auto instantiate_explicit_candidate =
		[&](const ExplicitOverloadCandidate& candidate) -> std::optional<ASTNode> {
		if (candidate.template_func == nullptr) {
			return std::nullopt;
		}
		auto existing_inst = gTemplateRegistry.getInstantiation(candidate.key);
		if (existing_inst.has_value()) {
			return *existing_inst;
		}
		const TemplateFunctionDeclarationNode& template_func = *candidate.template_func;
		const FunctionDeclarationNode& func_decl = template_func.function_decl_node();
		const auto& template_params = template_func.template_parameters();
		std::optional<CallArgDeductionInfo> helper_deduction_info = candidate.deduction_info;
		std::optional<TypeSpecifierNode> reparsed_trailing_return_type = candidate.reparsed_trailing_return_type;
		std::span<const size_t> template_param_arg_starts_view(candidate.template_param_arg_starts);
		std::span<const size_t> template_param_arg_counts_view(candidate.template_param_arg_counts);
		FunctionTemplateInstantiationContext instantiation_context{
			template_name,
			template_func,
			func_decl,
			template_params,
			candidate.template_args,
			candidate.key,
			candidate.overload_id,
			recursion_depth};
		FunctionTemplateBindingData binding_data{
			nullptr,
			&template_param_arg_starts_view,
			&template_param_arg_counts_view,
			&helper_deduction_info,
			&reparsed_trailing_return_type};
		FunctionTemplateInstantiationFlags instantiation_flags = mergeInstantiationFlags(
			FunctionTemplateInstantiationFlags::PreserveRefQualifier,
			FunctionTemplateInstantiationFlags::ExplicitMaterialization);
		instantiation_flags = mergeInstantiationFlags(
			instantiation_flags,
			FunctionTemplateInstantiationFlags::CommitInstantiation);
		instantiation_flags = mergeInstantiationFlags(
			instantiation_flags,
			FunctionTemplateInstantiationFlags::RegisterInstantiation);
		return instantiateBoundFunctionTemplate(
			instantiation_context,
			binding_data,
			instantiation_flags);
	};

	std::optional<size_t> preferred_candidate_index;
	if (use_shape_preselection) {
		std::vector<ASTNode> shape_overloads;
		std::vector<size_t> shape_candidate_indices;
		shape_overloads.reserve(viable_candidates.size());
		shape_candidate_indices.reserve(viable_candidates.size());
		for (size_t i = 0; i < viable_candidates.size(); ++i) {
			const ExplicitOverloadCandidate& candidate = viable_candidates[i];
			if (candidate.template_func == nullptr) {
				continue;
			}
			const TemplateFunctionDeclarationNode& template_func = *candidate.template_func;
			const FunctionDeclarationNode& func_decl = template_func.function_decl_node();
			const auto& template_params = template_func.template_parameters();
			std::optional<CallArgDeductionInfo> helper_deduction_info = candidate.deduction_info;
			std::optional<TypeSpecifierNode> reparsed_trailing_return_type = candidate.reparsed_trailing_return_type;
			std::span<const size_t> template_param_arg_starts_view(candidate.template_param_arg_starts);
			std::span<const size_t> template_param_arg_counts_view(candidate.template_param_arg_counts);
			StringBuilder shape_name_builder;
			StringHandle shape_name_handle = StringTable::getOrInternStringHandle(
				shape_name_builder
					.append(template_name)
					.append("$explicit_shape_ol")
					.append(static_cast<uint64_t>(candidate.overload_idx))
					.commit());
			FlashCpp::TemplateInstantiationKey shape_key = FlashCpp::makeInstantiationKey(
				shape_name_handle,
				candidate.template_args);
			FunctionTemplateInstantiationContext instantiation_context{
				template_name,
				template_func,
				func_decl,
				template_params,
				candidate.template_args,
				shape_key,
				candidate.overload_id,
				recursion_depth};
			FunctionTemplateBindingData binding_data{
				nullptr,
				&template_param_arg_starts_view,
				&template_param_arg_counts_view,
				&helper_deduction_info,
				&reparsed_trailing_return_type};
			FunctionTemplateInstantiationFlags shape_flags = mergeInstantiationFlags(
				FunctionTemplateInstantiationFlags::SkipBodyMaterialization,
				FunctionTemplateInstantiationFlags::PreserveRefQualifier);
			shape_flags = mergeInstantiationFlags(
				shape_flags,
				FunctionTemplateInstantiationFlags::ExplicitMaterialization);
			ScopedParserInstantiationContext shape_guard(
				*this,
				TemplateInstantiationMode::HardUseCandidateProbe,
				StringHandle{});
			std::optional<ASTNode> shape_node = instantiateBoundFunctionTemplate(
				instantiation_context,
				binding_data,
				shape_flags);
			if (!shape_node.has_value() ||
				!shape_node->is<FunctionDeclarationNode>() ||
				shape_node->as<FunctionDeclarationNode>().failed_substitution()) {
				continue;
			}
			shape_overloads.push_back(*shape_node);
			shape_candidate_indices.push_back(i);
		}
		if (shape_overloads.size() == 1) {
			preferred_candidate_index = shape_candidate_indices[0];
		} else if (shape_overloads.size() > 1) {
			auto resolution = resolve_overload(shape_overloads, *current_explicit_call_arg_types_);
			if (resolution.has_match &&
				!resolution.is_ambiguous &&
				resolution.selected_overload != nullptr) {
				for (size_t i = 0; i < shape_overloads.size(); ++i) {
					if (resolution.selected_overload == &shape_overloads[i]) {
						preferred_candidate_index = shape_candidate_indices[i];
						break;
					}
				}
			}
		}
	}

	if (preferred_candidate_index.has_value() &&
		*preferred_candidate_index < viable_candidates.size()) {
		std::optional<ASTNode> preferred_instantiation =
			instantiate_explicit_candidate(viable_candidates[*preferred_candidate_index]);
		if (preferred_instantiation.has_value()) {
			return *preferred_instantiation;
		}
	}

	for (size_t i = 0; i < viable_candidates.size(); ++i) {
		if (preferred_candidate_index.has_value() && i == *preferred_candidate_index) {
			continue;
		}
		std::optional<ASTNode> instantiated =
			instantiate_explicit_candidate(viable_candidates[i]);
		if (instantiated.has_value()) {
			return *instantiated;
		}
	}

	return std::nullopt;	 // No overload matched
}

std::optional<ASTNode> Parser::try_instantiate_template_explicit(
	std::string_view template_name,
	std::span<const TemplateTypeArg> explicit_types,
	std::span<const TypeSpecifierNode> arg_types) {
	FlashCpp::ScopedState guard_explicit_call_arg_types(current_explicit_call_arg_types_);
	current_explicit_call_arg_types_ = &arg_types;
	return try_instantiate_template_explicit(template_name, explicit_types, arg_types.size());
}

// Try to instantiate a function template with the given argument types
// Returns the instantiated function declaration node if successful
std::optional<ASTNode> Parser::try_instantiate_template(std::string_view template_name, std::span<const TypeSpecifierNode> arg_types) {
	PROFILE_TEMPLATE_INSTANTIATION(std::string(template_name) + "_func");

	static int recursion_depth = 0;
	recursion_depth++;
	struct DepthGuard {
		int& depth;
		~DepthGuard() { depth--; }
	} depth_guard{recursion_depth};

	if (recursion_depth > 64) {
		FLASH_LOG(Templates, Error, "try_instantiate_template recursion depth exceeded 64! Possible infinite loop for template '", template_name, "'");
		return std::nullopt;
	}

	std::vector<ASTNode> all_templates =
		materializeFunctionTemplateCandidateDeclarations(
			lookupFunctionTemplateCandidatesForInstantiation(template_name, recursion_depth));
	if (all_templates.empty()) {
		// This is expected for regular (non-template) functions - the caller will fall back
		// to creating a forward declaration. Only log at Debug level to avoid noise.
		FLASH_LOG(Templates, Debug, "[depth=", recursion_depth, "]: Template '", template_name, "' not found in registry");
		return std::nullopt;
	}

	FLASH_LOG_FORMAT(Templates, Debug, "[depth={}]: Found {} template overload(s) for '{}'",
					 recursion_depth, all_templates.size(), template_name);

	// Try each template overload in order.
	// For SFINAE: collect all viable matches and return the most specific one.
	// For non-SFINAE: return the first successful non-deferred match.
	bool outer_sfinae_context = template_instantiation_mode_ == TemplateInstantiationMode::SoftProbe;

	struct SfinaeCandidateEntry {
		ASTNode result;
		int specificity;
		bool is_deleted;
		size_t overload_idx;
	};
	std::vector<SfinaeCandidateEntry> sfinae_candidates;

	std::vector<size_t> overload_iteration_order;
	overload_iteration_order.resize(all_templates.size());
	std::iota(overload_iteration_order.begin(), overload_iteration_order.end(), size_t{0});
	if (!outer_sfinae_context) {
		constexpr int kNonFunctionTemplateScore = -1;

		std::vector<int> scores;
		scores.reserve(all_templates.size());
		for (const auto& node : all_templates) {
			scores.push_back(node.is<TemplateFunctionDeclarationNode>()
				? computeTemplateFunctionSpecificity(node.as<TemplateFunctionDeclarationNode>())
				: kNonFunctionTemplateScore);
		}
		std::stable_sort(
			overload_iteration_order.begin(),
			overload_iteration_order.end(),
			[&](size_t lhs_idx, size_t rhs_idx) {
				return scores[lhs_idx] > scores[rhs_idx];
			});
	}

	if (!outer_sfinae_context && all_templates.size() > 1) {
		struct ShapeCandidate {
			size_t overload_idx;
			InlineVector<TemplateTypeArg, 4> template_args;
			FlashCpp::TemplateInstantiationKey key;
			uintptr_t overload_id;
			std::optional<CallArgDeductionInfo> deduction_info;
		};
		std::vector<ShapeCandidate> shape_candidates;
		std::vector<ASTNode> shape_overloads;
		shape_candidates.reserve(all_templates.size());
		shape_overloads.reserve(all_templates.size());
		StringHandle template_name_handle = StringTable::getOrInternStringHandle(template_name);

		for (size_t sorted_idx = 0; sorted_idx < overload_iteration_order.size(); ++sorted_idx) {
			size_t overload_idx = overload_iteration_order[sorted_idx];
			const ASTNode& template_node = all_templates[overload_idx];
			if (!template_node.is<TemplateFunctionDeclarationNode>()) {
				continue;
			}

			const TemplateFunctionDeclarationNode& template_func =
				template_node.as<TemplateFunctionDeclarationNode>();
			const auto& template_params = template_func.template_parameters();
			const FunctionDeclarationNode& func_decl = template_func.function_decl_node();

			auto deduction_candidate = deduceTemplateCandidateViability(
				template_params,
				func_decl,
				arg_types,
				recursion_depth);
			if (!deduction_candidate.has_value()) {
				continue;
			}

			InlineVector<TemplateTypeArg, 4> template_args = deduction_candidate->template_args;
			const bool has_structurally_dependent_template_args = std::any_of(
				template_args.begin(),
				template_args.end(),
				[](const TemplateTypeArg& arg) {
					return arg.is_dependent ||
						   arg.dependent_name.isValid() ||
						   arg.category() == TypeCategory::Auto ||
						   arg.category() == TypeCategory::DeclTypeAuto;
				});
			if (has_structurally_dependent_template_args) {
				continue;
			}

			auto key = FlashCpp::makeInstantiationKey(
				template_name_handle,
				template_args);
			const uintptr_t overload_id = reinterpret_cast<uintptr_t>(&func_decl);
			if (gTemplateRegistry.isFailedInstantiation(key, overload_id)) {
				continue;
			}

			std::optional<CallArgDeductionInfo> helper_deduction_info =
				std::move(deduction_candidate->deduction_info);
			FunctionTemplateInstantiationContext instantiation_context{
				template_name,
				template_func,
				func_decl,
				template_params,
				template_args,
				key,
				overload_id,
				recursion_depth};
			FunctionTemplateBindingData binding_data{
				&arg_types,
				nullptr,
				nullptr,
				&helper_deduction_info,
				nullptr};
			FunctionTemplateInstantiationFlags instantiation_flags =
				FunctionTemplateInstantiationFlags::SkipBodyMaterialization;

			ScopedParserInstantiationContext shape_guard(
				*this,
				TemplateInstantiationMode::HardUseCandidateProbe,
				StringHandle{});
			std::optional<ASTNode> signature_node = instantiateBoundFunctionTemplate(
				instantiation_context,
				binding_data,
				instantiation_flags);
			if (!signature_node.has_value() ||
				!signature_node->is<FunctionDeclarationNode>() ||
				signature_node->as<FunctionDeclarationNode>().failed_substitution()) {
				continue;
			}
			shape_candidates.push_back(ShapeCandidate{
				overload_idx,
				std::move(template_args),
				std::move(key),
				overload_id,
				std::move(helper_deduction_info)});
			shape_overloads.push_back(*signature_node);
		}

		if (shape_overloads.size() > 1) {
			auto resolution = resolve_overload(shape_overloads, arg_types);
			if (resolution.has_match &&
				!resolution.is_ambiguous &&
				resolution.selected_overload != nullptr) {
				for (size_t i = 0; i < shape_overloads.size(); ++i) {
					if (resolution.selected_overload == &shape_overloads[i]) {
						// Reuse the precomputed template_args from the shape probe to skip
						// redundant argument deduction for the winning candidate.
						ShapeCandidate& winner = shape_candidates[i];
						const ASTNode& winner_template_node = all_templates[winner.overload_idx];
						const TemplateFunctionDeclarationNode& winner_template_func =
							winner_template_node.as<TemplateFunctionDeclarationNode>();
						const FunctionDeclarationNode& winner_func_decl =
							winner_template_func.function_decl_node();
						const auto& winner_template_params =
							winner_template_func.template_parameters();

						// Check explicit specializations before body instantiation.
						auto specialization_opt = gTemplateRegistry.lookupSpecialization(
							template_name, winner.template_args);
						if (specialization_opt.has_value()) {
							FLASH_LOG_FORMAT(Templates, Debug,
								"[depth={}]: Shape-winner: found explicit specialization for '{}'",
								recursion_depth, template_name);
							return *specialization_opt;
						}

						// Evaluate requires clause and concept constraints.
						if (winner_template_func.has_requires_clause()) {
							const RequiresClauseNode& rc =
								winner_template_func.requires_clause()->as<RequiresClauseNode>();
							InlineVector<std::string_view, 4> param_names;
							for (const auto& tp : winner_template_params)
								param_names.push_back(tp.name());
							auto cr = evaluateConstraint(rc.constraint_expr(), winner.template_args, param_names);
							if (!cr.satisfied) {
								failTemplateInstantiation(
									StringBuilder()
										.append("constraint not satisfied for template function '")
										.append(template_name)
										.append("'")
										.commit(),
									&winner.key,
									winner.overload_id);
								break;
							}
						}
						{
							bool concept_failed = false;
							forEachNonPackTemplateParamArgBinding(
								winner_template_params,
								winner.template_args,
								[&](const TemplateParameterNode& param, const TemplateTypeArg& bound_arg, size_t) {
									if (!param.has_concept_constraint() || concept_failed)
										return;
									auto concept_opt = gConceptRegistry.lookupConcept(param.concept_constraint());
									if (!concept_opt.has_value())
										return;
									const auto& concept_node = concept_opt->as<ConceptDeclarationNode>();
									TemplateTypeArg concept_arg = bound_arg;
									concept_arg.ref_qualifier = ReferenceQualifier::None;
									InlineVector<TemplateTypeArg, 4> concept_args;
									concept_args.push_back(concept_arg);
									auto cr = evaluateConstraint(concept_node, concept_args);
									if (!cr.satisfied) {
										concept_failed = true;
									}
								});
							if (concept_failed) {
								failTemplateInstantiation(
									StringBuilder()
										.append("concept constraint not satisfied for template function '")
										.append(template_name)
										.append("'")
										.commit(),
									&winner.key,
									winner.overload_id);
								break;
							}
						}

						const bool cacheable = hasUsableTemplateFunctionDefinition(winner_func_decl);
						const bool commit = shouldCommitTemplateInstantiationArtifacts();
						FunctionTemplateInstantiationContext winner_ctx{
							template_name,
							winner_template_func,
							winner_func_decl,
							winner_template_params,
							winner.template_args,
							winner.key,
							winner.overload_id,
							recursion_depth};
						FunctionTemplateBindingData winner_binding{
							&arg_types,
							nullptr,
							nullptr,
							&winner.deduction_info,
							nullptr};
						FunctionTemplateInstantiationFlags winner_flags =
							FunctionTemplateInstantiationFlags::None;
						if (cacheable) {
							winner_flags = mergeInstantiationFlags(
								winner_flags,
								FunctionTemplateInstantiationFlags::CacheableInstantiation);
						}
						if (commit) {
							winner_flags = mergeInstantiationFlags(
								winner_flags,
								FunctionTemplateInstantiationFlags::CommitInstantiation);
						}
						winner_flags = mergeInstantiationFlags(
							winner_flags,
							FunctionTemplateInstantiationFlags::RegisterInstantiation);
						winner_flags = mergeInstantiationFlags(
							winner_flags,
							FunctionTemplateInstantiationFlags::MemoizeBodyReparseFailure);
						winner_flags = mergeInstantiationFlags(
							winner_flags,
							FunctionTemplateInstantiationFlags::RunInlineHeuristic);
						ScopedParserInstantiationContext guard_instantiation_mode(
							*this,
							selectTemplateCandidateProbeMode(),
							StringHandle{});
						std::optional<ASTNode> result = instantiateBoundFunctionTemplate(
							winner_ctx,
							winner_binding,
							winner_flags);
						if (result.has_value()) {
							FLASH_LOG_FORMAT(
								Templates,
								Debug,
								"[depth={}]: Shape-selected template overload {} for '{}'",
								recursion_depth,
								winner.overload_idx,
								template_name);
							return result;
						}
						break;
					}
				}
			}
		}
	}

	std::optional<ASTNode> deferred_forward_declaration_result;
	for (size_t sorted_idx = 0; sorted_idx < overload_iteration_order.size(); ++sorted_idx) {
		size_t overload_idx = overload_iteration_order[sorted_idx];
		const ASTNode& template_node = all_templates[overload_idx];

		if (!template_node.is<TemplateFunctionDeclarationNode>()) {
			FLASH_LOG_FORMAT(Templates, Debug, "[depth={}]: Skipping overload {} - not a function template",
							 recursion_depth, overload_idx);
			continue;
		}

		FLASH_LOG_FORMAT(Templates, Debug, "[depth={}]: Trying template overload {} for '{}'",
						 recursion_depth, overload_idx, template_name);

		// Enter the mode appropriate for this instantiation attempt.
		ScopedParserInstantiationContext guard_instantiation_mode(*this, selectTemplateCandidateProbeMode(), StringHandle{});

		// Try to instantiate this specific template
		std::optional<ASTNode> result = try_instantiate_single_template(
			template_node, template_name, arg_types, recursion_depth);

		if (result.has_value()) {
			const TemplateFunctionDeclarationNode& template_func =
				template_node.as<TemplateFunctionDeclarationNode>();
			const FunctionDeclarationNode& func_decl = template_func.function_decl_node();

			if (outer_sfinae_context) {
				// In SFINAE: collect all viable candidates for best-match selection.
				int spec = computeTemplateFunctionSpecificity(template_func);
				bool is_del = func_decl.is_deleted();
				FLASH_LOG_FORMAT(Templates, Debug,
					"[depth={}]: SFINAE candidate overload {} for '{}' specificity={} deleted={}",
					recursion_depth, overload_idx, template_name, spec, is_del);
				sfinae_candidates.push_back({*result, spec, is_del, overload_idx});
			} else {
				if (!hasUsableTemplateFunctionDefinition(func_decl) &&
					hasLaterUsableTemplateDefinitionWithMatchingShape(all_templates, overload_idx)) {
					FLASH_LOG_FORMAT(
						Templates,
						Debug,
						"[depth={}]: Deferring bodyless overload {} for '{}' until later matching definitions are checked",
						recursion_depth,
						overload_idx,
						template_name);
					if (!deferred_forward_declaration_result.has_value()) {
						deferred_forward_declaration_result = result;
					}
					continue;
				}
				// Non-SFINAE: success — return first good match.
				FLASH_LOG_FORMAT(Templates, Debug, "[depth={}]: Successfully instantiated overload {} for '{}'",
								 recursion_depth, overload_idx, template_name);
				return result;
			}
		} else {
			// Instantiation failed - try next overload (SFINAE)
			FLASH_LOG_FORMAT(Templates, Debug, "[depth={}]: Overload {} failed substitution, trying next",
							 recursion_depth, overload_idx);
		}
	}

	// SFINAE best-match selection: pick the most specific successful candidate.
	if (!sfinae_candidates.empty()) {
		int best_specificity = sfinae_candidates[0].specificity;
		for (const auto& candidate : sfinae_candidates) {
			if (candidate.specificity > best_specificity) {
				best_specificity = candidate.specificity;
			}
		}
		// Collect the best non-deleted candidate (if any) at the best specificity level.
		// If every best-specificity candidate is `= delete`, treat it as a SFINAE failure:
		// per C++17 [temp.deduct.call], selecting a deleted function in the immediate
		// context of a decltype/SFINAE probe is itself a substitution failure. The
		// reparse path has already rejected overloads whose return-type substitution
		// failed (e.g. `enable_if<false>::type`), so any `= delete` candidate that
		// reaches this collection step is a genuine explicit SFINAE sentinel.
		const SfinaeCandidateEntry* best_non_deleted = nullptr;
		for (const auto& candidate : sfinae_candidates) {
			if (candidate.specificity == best_specificity && !candidate.is_deleted) {
				best_non_deleted = &candidate;
				break;
			}
		}
		if (!best_non_deleted) {
			FLASH_LOG_FORMAT(Templates, Debug,
				"[depth={}]: SFINAE failure for '{}': all best-specificity candidates are = delete (specificity={})",
				recursion_depth, template_name, best_specificity);
			return std::nullopt;
		}
		FLASH_LOG_FORMAT(Templates, Debug,
			"[depth={}]: SFINAE best match for '{}' is overload {} specificity={} deleted=false",
			recursion_depth, template_name, best_non_deleted->overload_idx, best_specificity);
		return best_non_deleted->result;
	}

	if (deferred_forward_declaration_result.has_value()) {
		FLASH_LOG_FORMAT(
			Templates,
			Debug,
			"[depth={}]: Falling back to deferred bodyless overload for '{}'",
			recursion_depth,
			template_name);
		return deferred_forward_declaration_result;
	}

	// All overloads failed
	FLASH_LOG_FORMAT(Templates, Error, "[depth={}]: All {} template overload(s) failed for '{}'",
					 recursion_depth, all_templates.size(), template_name);
	return std::nullopt;
}

std::optional<InlineVector<TemplateTypeArg, 4>> Parser::deduceTemplateArgsFromCall(
	const InlineVector<TemplateParameterNode, 4>& template_params,
	std::span<const TypeSpecifierNode> arg_types,
	const CallArgDeductionInfo& deduction_info,
	size_t function_pack_arg_start,
	int recursion_depth,
	NamespaceHandle source_namespace) {

	InlineVector<TemplateTypeArg, 4> template_args;
	std::vector<TypeCategory> deduced_type_args;
	size_t next_deduced_type_arg = 0;
	std::vector<TemplateTypeArg> deduced_value_args;
	size_t next_deduced_value_arg = 0;
	const auto& param_name_to_arg = deduction_info.param_name_to_arg;
	const auto& pre_deduced_arg_indices = deduction_info.pre_deduced_arg_indices;
	size_t arg_index = 0;

	const auto skipPreDeducedArgs = [&]() {
		while (arg_index < arg_types.size() && pre_deduced_arg_indices.count(arg_index)) {
			++arg_index;
		}
	};
	const auto tryAppendPreDeducedArg = [&](StringHandle param_handle) {
		auto map_it = param_name_to_arg.find(param_handle);
		if (map_it == param_name_to_arg.end()) {
			return false;
		}
		template_args.push_back(map_it->second);
		return true;
	};

	for (const TemplateParameterNode& param : template_params) {
		if (param.kind() == TemplateParameterKind::Template) {
			skipPreDeducedArgs();
			if (arg_index >= arg_types.size()) {
				FLASH_LOG(Templates, Error, "[depth=", recursion_depth, "]: Not enough arguments to deduce template template parameter");
				return std::nullopt;
			}

			const TypeSpecifierNode& arg_type = arg_types[arg_index];
			if (arg_type.category() != TypeCategory::Struct) {
				FLASH_LOG(Templates, Error, "[depth=", recursion_depth, "]: Template template parameter requires struct argument, got type ", static_cast<int>(arg_type.type()));
				return std::nullopt;
			}

			TypeIndex type_index = arg_type.type_index();
			const TypeInfo* type_info = tryGetTypeInfo(type_index);
			if (type_info == nullptr) {
				FLASH_LOG(Templates, Error, "[depth=", recursion_depth, "]: Invalid type index ", static_cast<int>(type_index.index()));
				return std::nullopt;
			}
			if (!type_info->isTemplateInstantiation()) {
				std::string_view type_name = StringTable::getStringView(type_info->name());
				FLASH_LOG(Templates, Error, "[depth=", recursion_depth, "]: Type '", type_name, "' is not a template instantiation");
				return std::nullopt;
			}

			StringHandle inner_template_name = type_info->baseTemplateName();
			auto template_check = gTemplateRegistry.lookupTemplate(inner_template_name);
			if (!template_check.has_value()) {
				FLASH_LOG(Templates, Error, "[depth=", recursion_depth, "]: Template '", inner_template_name, "' not found");
				return std::nullopt;
			}

			template_args.push_back(TemplateTypeArg::makeTemplate(inner_template_name));
			const auto& stored_args = type_info->templateArgs();
			for (const auto& stored_arg : stored_args) {
				if (stored_arg.is_value) {
					deduced_value_args.push_back(toTemplateTypeArg(stored_arg));
				} else {
					deduced_type_args.push_back(stored_arg.typeEnum());
				}
			}
			++arg_index;
			continue;
		}

		if (param.kind() == TemplateParameterKind::Type) {
			if (param.is_variadic()) {
				// Gate call-arg consumption on the function-parameter pack.
				// If this param is NOT in the set of dependent pack names for the
				// function-parameter pack element type, it cannot be deduced from call args.
				// Produce an empty pack and continue.
				// The set contains the primary pack name for simple "Ts... args" cases and
				// ALL dependent pack names for multi-dependent types like "Pair<Ts,Us>...".
				if (!deduction_info.function_pack_dependent_param_names.count(param.nameHandle())) {
					continue;
				}
				if (function_pack_arg_start != SIZE_MAX) {
					arg_index = function_pack_arg_start;
				}
				while (arg_index < arg_types.size()) {
					if (pre_deduced_arg_indices.count(arg_index)) {
						++arg_index;
						continue;
					}
					const TypeSpecifierNode& ca_type = arg_types[arg_index];
					bool pushed = false;
					if (auto extracted_arg = extractNestedTemplateArgForDependentName(
							deduction_info.function_pack_element_type_index,
							ca_type.type_index(),
							param.nameHandle())) {
						template_args.push_back(*extracted_arg);
						pushed = true;
					}
					if (!pushed) {
						template_args.push_back(TemplateTypeArg::makeTypeSpecifier(ca_type));
					}
					++arg_index;
				}
				continue;
			}

			StringHandle param_handle = param.nameHandle();
			if (tryAppendPreDeducedArg(param_handle)) {
				continue;
			}
			if (next_deduced_type_arg < deduced_type_args.size()) {
				TypeCategory deduced_type = deduced_type_args[next_deduced_type_arg++];
				template_args.push_back(TemplateTypeArg::makeType(nativeTypeIndex(deduced_type)));
				continue;
			}

			skipPreDeducedArgs();
			if (arg_index < arg_types.size() &&
				deduction_info.positional_deducible_param_names.count(param_handle)) {
				template_args.push_back(TemplateTypeArg::makeTypeSpecifier(arg_types[arg_index]));
				++arg_index;
				continue;
			}
			if (tryAppendDefaultTemplateArg(param, template_params, template_args, source_namespace)) {
				continue;
			}
			return std::nullopt;
		}

		StringHandle param_handle = param.nameHandle();
		if (tryAppendPreDeducedArg(param_handle)) {
			continue;
		}
		if (next_deduced_value_arg < deduced_value_args.size()) {
			template_args.push_back(deduced_value_args[next_deduced_value_arg++]);
			continue;
		}
		if (tryAppendDefaultTemplateArg(param, template_params, template_args, source_namespace)) {
			continue;
		}

		// SFINAE: a non-type template parameter could not be deduced from the
		// call arguments and has no usable default.  Callers treat std::nullopt
		// as "this overload does not match", so this path is part of normal
		// overload resolution and must not be logged as an error.
		std::string_view param_name_sv = StringTable::getStringView(param_handle);
		FLASH_LOG_FORMAT(Templates, Debug,
			"[depth={}]: SFINAE: non-type template parameter '{}' could not be deduced",
			recursion_depth, param_name_sv);
		return std::nullopt;
	}

	return template_args;
}

std::optional<Parser::TemplateDeductionCandidate> Parser::deduceTemplateCandidateViability(
	const InlineVector<TemplateParameterNode, 4>& template_params,
	std::span<const ASTNode> func_params,
	std::span<const TypeSpecifierNode> arg_types,
	NamespaceHandle source_namespace,
	int recursion_depth) {
	bool all_variadic = true;
	for (const TemplateParameterNode& template_param : template_params) {
		if (!template_param.is_variadic()) {
			all_variadic = false;
			break;
		}
	}
	if (arg_types.empty() && !all_variadic) {
		return std::nullopt;
	}

	size_t min_required_args = 0;
	size_t non_pack_params = 0;
	bool has_function_parameter_pack = false;
	for (const ASTNode& param_node : func_params) {
		if (!param_node.is<DeclarationNode>()) {
			continue;
		}
		const DeclarationNode& param_decl = param_node.as<DeclarationNode>();
		if (isTemplateFunctionParameterPack(template_params, param_decl)) {
			has_function_parameter_pack = true;
			continue;
		}
		++non_pack_params;
		if (!param_decl.has_default_value()) {
			++min_required_args;
		}
	}
	if (arg_types.size() < min_required_args ||
		(!has_function_parameter_pack && arg_types.size() > non_pack_params)) {
		FLASH_LOG_FORMAT(Templates, Debug,
			"[depth={}]: SFINAE: argument count {} is not viable for template candidate "
			"(required={}, fixed_params={}, has_pack={})",
			recursion_depth,
			arg_types.size(),
			min_required_args,
			non_pack_params,
			has_function_parameter_pack);
		return std::nullopt;
	}

	size_t function_pack_arg_start = SIZE_MAX;
	if (has_function_parameter_pack) {
		size_t params_before_pack = 0;
		for (const ASTNode& param_node : func_params) {
			if (param_node.is<DeclarationNode>() &&
				isTemplateFunctionParameterPack(template_params, param_node.as<DeclarationNode>())) {
				break;
			}
			if (param_node.is<DeclarationNode>()) {
				++params_before_pack;
			}
		}
		function_pack_arg_start = std::min(arg_types.size(), params_before_pack);
	}

		auto deduction_info = buildDeductionMapFromCallArgs(
			template_params,
			func_params,
			arg_types,
			recursion_depth,
			nullptr);
	if (!deduction_info.has_value()) {
		return std::nullopt;
	}

	auto template_args = deduceTemplateArgsFromCall(
		template_params,
		arg_types,
		*deduction_info,
		function_pack_arg_start,
		recursion_depth,
		source_namespace);
	if (!template_args.has_value()) {
		return std::nullopt;
	}

	// This shape-only context is deliberately not used to materialize anything.
	// It makes the candidate viability/deduction boundary explicit and gives
	// later substitution checks a single TemplateInstantiationContext seed.
	TemplateInstantiationContext deduction_context = buildTemplateInstantiationContext(
		template_params,
		std::span<const TemplateTypeArg>(template_args->data(), template_args->size()),
		nullptr,
		TemplateSubstitutionFailurePolicy::ShapeOnly);
	(void)deduction_context;

	TemplateDeductionCandidate candidate;
	candidate.template_args = std::move(*template_args);
	candidate.deduction_info = std::move(*deduction_info);
	candidate.function_pack_arg_start = function_pack_arg_start;
	return candidate;
}

std::optional<Parser::TemplateDeductionCandidate> Parser::deduceTemplateCandidateViability(
	const InlineVector<TemplateParameterNode, 4>& template_params,
	const FunctionDeclarationNode& func_decl,
	std::span<const TypeSpecifierNode> arg_types,
	int recursion_depth) {
	if (!functionTemplateAcceptsCallArgumentCount(template_params, func_decl, arg_types.size())) {
		size_t required_params = countMinRequiredArgs(func_decl);
		if (arg_types.size() < required_params) {
			FLASH_LOG_FORMAT(Templates, Debug,
				"[depth={}]: SFINAE: argument count {} < required parameter count {}",
				recursion_depth, arg_types.size(), required_params);
		} else {
			FLASH_LOG_FORMAT(Templates, Debug,
				"[depth={}]: SFINAE: argument count {} > parameter count {}",
				recursion_depth, arg_types.size(), func_decl.parameter_nodes().size());
		}
		return std::nullopt;
	}
	return deduceTemplateCandidateViability(
		template_params,
		func_decl.parameter_nodes(),
		arg_types,
		func_decl.namespace_handle(),
		recursion_depth);
}

std::optional<Parser::TemplateDeductionCandidate> Parser::deduceTemplateCandidateViability(
	const InlineVector<TemplateParameterNode, 4>& template_params,
	const ConstructorDeclarationNode& ctor_decl,
	std::span<const TypeSpecifierNode> arg_types,
	int recursion_depth) {
	return deduceTemplateCandidateViability(
		template_params,
		ctor_decl.parameter_nodes(),
		arg_types,
		NamespaceHandle{},
		recursion_depth);
}

// Helper function: Try to instantiate a specific template node
// This contains the core instantiation logic extracted from try_instantiate_template
// Returns nullopt if instantiation fails (for SFINAE)
std::optional<ASTNode> Parser::try_instantiate_single_template(
	const ASTNode& template_node,
	std::string_view template_name,
	std::span<const TypeSpecifierNode> arg_types,
	int& recursion_depth) {
	const TemplateFunctionDeclarationNode& template_func = template_node.as<TemplateFunctionDeclarationNode>();
	const auto& template_params = template_func.template_parameters();
	const FunctionDeclarationNode& func_decl = template_func.function_decl_node();

	// Step 1: Deduce template arguments from function call arguments.
	// Type parameters (TemplateParameterKind::Type) and non-type parameters
	// (TemplateParameterKind::NonType) are both handled by buildDeductionMapFromCallArgs
	// and deduceTemplateArgsFromCall below.  The NOTE below is historical context
	// only; the helpers already cover non-type deduction via the is_value path.
	// More complex deduction (partial ordering, template-template params) is still
	// limited; see Phase 6 audit in docs/2026-04-21-phase5-slice-g-analysis.md.

	auto deduction_candidate = deduceTemplateCandidateViability(
		template_params,
		func_decl,
		arg_types,
		recursion_depth);
	if (!deduction_candidate.has_value()) {
		return std::nullopt;
	}
	InlineVector<TemplateTypeArg, 4> template_args = std::move(deduction_candidate->template_args);
	std::optional<CallArgDeductionInfo> deduction_info = std::move(deduction_candidate->deduction_info);
	// template_args is already std::vector<TemplateTypeArg> — no conversion needed.
	const auto has_structurally_dependent_template_args = [](std::span<const TemplateTypeArg> args) {
		return std::any_of(
			args.begin(),
			args.end(),
			[](const TemplateTypeArg& arg) {
				return arg.is_dependent ||
					   arg.dependent_name.isValid() ||
					   arg.category() == TypeCategory::Auto ||
					   arg.category() == TypeCategory::DeclTypeAuto;
			});
	};
	if (has_structurally_dependent_template_args(template_args)) {
		return std::nullopt;
	}

	// Step 2: Check if we already have this instantiation
	auto key = FlashCpp::makeInstantiationKey(
		StringTable::getOrInternStringHandle(template_name), template_args);
	// Per-overload discriminator for the SFINAE failure memo.  Using the
	// address of the overload's FunctionDeclarationNode separates overloads
	// of the same template name whose deduced arg lists and arities are
	// identical but whose SFINAE predicates are not (e.g. two `process(T)`
	// overloads, one enabled for `is_int<T>` and the other for `is_double<T>`).
	const uintptr_t overload_id = reinterpret_cast<uintptr_t>(&func_decl);

	// SFINAE fast-path: if a previous probe against *this same overload*
	// already recorded a substitution failure for the same deduced args,
	// skip the entire reparse.  This is the N²-reparse elimination.
	if (gTemplateRegistry.isFailedInstantiation(key, overload_id)) {
		FLASH_LOG_FORMAT(Templates, Debug,
			"[depth={}]: SFINAE fast-path: '{}' previously failed substitution for this overload",
			recursion_depth, template_name);
		return std::nullopt;
	}

	const bool cacheable_instantiation = hasUsableTemplateFunctionDefinition(func_decl);

	if (cacheable_instantiation) {
		auto existing_inst = gTemplateRegistry.getInstantiation(key);
		if (existing_inst.has_value()) {
			// Defensive: if a future caller ever registers a FailedSubstitution
			// node under this key, treat it as a SFINAE miss rather than a hit.
			if (existing_inst->is<FunctionDeclarationNode>() &&
				existing_inst->as<FunctionDeclarationNode>().failed_substitution()) {
				FLASH_LOG_FORMAT(Templates, Debug,
					"[depth={}]: cached instantiation for '{}' is FailedSubstitution — skipping",
					recursion_depth, template_name);
				return std::nullopt;
			}
			PROFILE_TEMPLATE_CACHE_HIT(std::string(template_name) + "_func");
			return *existing_inst;  // Return existing instantiation
		}
		PROFILE_TEMPLATE_CACHE_MISS(std::string(template_name) + "_func");
	} else {
		FLASH_LOG_FORMAT(
			Templates,
			Debug,
			"[depth={}]: Not caching declaration-only instantiation for '{}'",
			recursion_depth,
			template_name);
	}

	// Check for explicit specialization before instantiating the primary template.
	auto specialization_opt = gTemplateRegistry.lookupSpecialization(template_name, template_args);
	if (specialization_opt.has_value()) {
		FLASH_LOG(Templates, Debug, "[depth=", recursion_depth, "]: Found explicit specialization for deduced args of '", template_name, "'");
		return *specialization_opt;
	}

	// CHECK REQUIRES CLAUSE CONSTRAINT BEFORE INSTANTIATION
	FLASH_LOG(Templates, Debug, "Checking requires clause for template function '", template_name, "', has_requires_clause=", template_func.has_requires_clause());
	if (template_func.has_requires_clause()) {
		const RequiresClauseNode& requires_clause =
			template_func.requires_clause()->as<RequiresClauseNode>();

		// Get template parameter names for evaluation
		InlineVector<std::string_view, 4> eval_param_names;
		for (const auto& tparam_node : template_params) {
			eval_param_names.push_back(tparam_node.name());
		}

		FLASH_LOG(Templates, Debug, "  Evaluating constraint with ", template_args.size(), " template args and ", eval_param_names.size(), " param names");

		// Evaluate the constraint with the template arguments
		auto constraint_result = evaluateConstraint(
			requires_clause.constraint_expr(), template_args, eval_param_names);

		FLASH_LOG(Templates, Debug, "  Constraint evaluation result: satisfied=", constraint_result.satisfied);

		if (!constraint_result.satisfied) {
			// Constraint not satisfied - report detailed error
			// Build template arguments string
			std::string args_str;
			for (size_t i = 0; i < template_args.size(); ++i) {
				if (i > 0)
					args_str += ", ";
				args_str += template_args[i].toString();
			}

			FLASH_LOG(Parser, Error, "constraint not satisfied for template function '", template_name, "'");
			FLASH_LOG(Parser, Error, "  ", constraint_result.error_message);
			if (!constraint_result.failed_requirement.empty()) {
				FLASH_LOG(Parser, Error, "  failed requirement: ", constraint_result.failed_requirement);
			}
			if (!constraint_result.suggestion.empty()) {
				FLASH_LOG(Parser, Error, "  suggestion: ", constraint_result.suggestion);
			}
			FLASH_LOG(Parser, Error, "  template arguments: ", args_str);

			// Don't create instantiation - constraint failed.
			// Memoize under a per-overload key so repeat SFINAE probes for the
			// same (template_name, args, overload) tuple short-circuit at the
			// entry fast-path instead of re-evaluating the requires-clause.
			// The failure is deterministic in those keys: the requires-clause
			// is a pure predicate over the deduced template arguments.
			return failTemplateInstantiation(
				StringBuilder()
					.append("constraint not satisfied for template function '")
					.append(template_name)
					.append("'")
					.commit(),
				&key,
				overload_id);
		}
	}

	// CHECK CONCEPT CONSTRAINTS ON TEMPLATE PARAMETERS (C++20 abbreviated templates)
	{
		bool concept_failed = false;
		forEachNonPackTemplateParamArgBinding(
			template_params,
			template_args,
			[&](const TemplateParameterNode& param, const TemplateTypeArg& bound_arg, size_t) {
				if (!param.has_concept_constraint() || concept_failed)
					return;
				std::string_view concept_name = param.concept_constraint();
				auto concept_opt = gConceptRegistry.lookupConcept(concept_name);
				if (!concept_opt.has_value())
					return;
				const auto& concept_node = concept_opt->as<ConceptDeclarationNode>();
				TemplateTypeArg concept_arg = bound_arg;
				concept_arg.ref_qualifier = ReferenceQualifier::None;
				InlineVector<TemplateTypeArg, 4> concept_args;
				concept_args.push_back(concept_arg);
				auto constraint_result = evaluateConstraint(
					concept_node, concept_args);
				if (!constraint_result.satisfied) {
					FLASH_LOG(Parser, Error, "concept constraint '", concept_name, "' not satisfied for parameter '", param.name(), "' of '", template_name, "'");
					FLASH_LOG(Parser, Error, "  ", constraint_result.error_message);
					concept_failed = true;
				}
			});
		if (concept_failed) {
			// Memoize under the per-overload key: the concept predicate is
			// pure in the deduced template arguments, so a repeat probe for
			// this same (template_name, args, overload) tuple will fail
			// identically.  Short-circuiting at the entry fast-path avoids
			// redoing concept evaluation on every SFINAE retry.
			return failTemplateInstantiation(
				StringBuilder()
					.append("concept constraint not satisfied for template function '")
					.append(template_name)
					.append("'")
					.commit(),
				&key,
				overload_id);
		}
	}

	const bool commit_instantiation = shouldCommitTemplateInstantiationArtifacts();
	std::optional<CallArgDeductionInfo> helper_deduction_info = deduction_info;
	FunctionTemplateInstantiationContext instantiation_context{
		template_name,
		template_func,
		func_decl,
		template_params,
		template_args,
		key,
		overload_id,
		recursion_depth};
	FunctionTemplateBindingData binding_data{
		&arg_types,
		nullptr,
		nullptr,
		&helper_deduction_info,
		nullptr};
	FunctionTemplateInstantiationFlags instantiation_flags = FunctionTemplateInstantiationFlags::None;
	if (cacheable_instantiation) {
		instantiation_flags = mergeInstantiationFlags(
			instantiation_flags,
			FunctionTemplateInstantiationFlags::CacheableInstantiation);
	}
	if (commit_instantiation) {
		instantiation_flags = mergeInstantiationFlags(
			instantiation_flags,
			FunctionTemplateInstantiationFlags::CommitInstantiation);
	}
	instantiation_flags = mergeInstantiationFlags(
		instantiation_flags,
		FunctionTemplateInstantiationFlags::RegisterInstantiation);
	instantiation_flags = mergeInstantiationFlags(
		instantiation_flags,
		FunctionTemplateInstantiationFlags::MemoizeBodyReparseFailure);
	instantiation_flags = mergeInstantiationFlags(
		instantiation_flags,
		FunctionTemplateInstantiationFlags::RunInlineHeuristic);
	return instantiateBoundFunctionTemplate(
		instantiation_context,
		binding_data,
		instantiation_flags);
}

// Get the mangled name for an instantiated class template using hash-based naming
// Example: Container<int> -> Container$a1b2c3d4 (hash-based, unambiguous)
