#include "Parser.h"
#include "ConstExprEvaluator.h"
#include <span>
#include "ExpressionSubstitutor.h"
#include "NameMangling.h"
#include "OverloadResolution.h"
#include "TypeTraitEvaluator.h"

TemplateTypeArg templateTypeArgFromEvalResult(const ConstExpr::EvalResult& eval_result) {
	if (const auto* bool_value = std::get_if<bool>(&eval_result.value)) {
		return TemplateTypeArg(*bool_value ? 1LL : 0LL, TypeCategory::Bool);
	}
	if (const auto* uint_value = std::get_if<unsigned long long>(&eval_result.value)) {
		TypeCategory value_category = eval_result.exact_type.has_value()
			? eval_result.exact_type->category()
			: TypeCategory::UnsignedLongLong;
		return TemplateTypeArg(static_cast<int64_t>(*uint_value), value_category);
	}
	if (eval_result.exact_type.has_value()) {
		return TemplateTypeArg(eval_result.as_int(), eval_result.exact_type->category());
	}
	return TemplateTypeArg(eval_result.as_int());
}

namespace {

template <typename ParamContainer>
ASTNode substituteNonTypeDefaultExpressionImpl(
	Parser& parser,
	const ASTNode& default_node,
	const ParamContainer& template_params,
	std::span<const TemplateTypeArg> template_args) {
	if (!default_node.is<ExpressionNode>() || template_args.empty()) {
		return default_node;
	}

	auto sub_map = buildSubstitutionParamMap(template_params, template_args);
	if (sub_map.empty()) {
		return default_node;
	}

	ExpressionSubstitutor substitutor(sub_map.param_map, parser, sub_map.param_order);
	return substitutor.substitute(default_node);
}

template <typename ParamContainer>
std::optional<TemplateTypeArg> substituteAndEvaluateNonTypeDefaultImpl(
	Parser& parser,
	const ASTNode& default_node,
	const ParamContainer& template_params,
	std::span<const TemplateTypeArg> template_args,
	std::span<const std::string_view> template_param_names) {
	ASTNode substituted_default_node = substituteNonTypeDefaultExpressionImpl(
		parser,
		default_node,
		template_params,
		template_args);
	if (!substituted_default_node.is<ExpressionNode>()) {
		return std::nullopt;
	}

	ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
	eval_ctx.parser = &parser;
	eval_ctx.sema = parser.getActiveSemanticAnalysis();
	eval_ctx.template_args.assign(template_args.begin(), template_args.end());
	eval_ctx.template_param_names.assign(
		template_param_names.begin(),
		template_param_names.end());

	auto eval_result = ConstExpr::Evaluator::evaluate(substituted_default_node, eval_ctx);
	if (!eval_result.success()) {
		return std::nullopt;
	}

	return templateTypeArgFromEvalResult(eval_result);
}

}  // namespace

ASTNode Parser::substituteNonTypeDefaultExpression(
	const ASTNode& default_node,
	const std::vector<ASTNode>& template_params,
	std::span<const TemplateTypeArg> template_args) {
	return substituteNonTypeDefaultExpressionImpl(
		*this,
		default_node,
		template_params,
		template_args);
}

ASTNode Parser::substituteNonTypeDefaultExpression(
	const ASTNode& default_node,
	const InlineVector<ASTNode, 4>& template_params,
	std::span<const TemplateTypeArg> template_args) {
	return substituteNonTypeDefaultExpressionImpl(
		*this,
		default_node,
		template_params,
		template_args);
}

std::optional<TemplateTypeArg> Parser::substituteAndEvaluateNonTypeDefault(
	const ASTNode& default_node,
	const std::vector<ASTNode>& template_params,
	std::span<const TemplateTypeArg> template_args,
	std::span<const std::string_view> template_param_names) {
	return substituteAndEvaluateNonTypeDefaultImpl(
		*this,
		default_node,
		template_params,
		template_args,
		template_param_names);
}

std::optional<TemplateTypeArg> Parser::substituteAndEvaluateNonTypeDefault(
	const ASTNode& default_node,
	const InlineVector<ASTNode, 4>& template_params,
	std::span<const TemplateTypeArg> template_args,
	std::span<const std::string_view> template_param_names) {
	return substituteAndEvaluateNonTypeDefaultImpl(
		*this,
		default_node,
		template_params,
		template_args,
		template_param_names);
}

std::string_view Parser::get_instantiated_class_name(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args) {
	if (size_t last_colon = template_name.rfind("::"); last_colon != std::string_view::npos) {
		template_name = template_name.substr(last_colon + 2);
	}
	auto result = FlashCpp::generateInstantiatedNameFromArgs(template_name, template_args);
	return result;
}

std::optional<TemplateTypeArg> Parser::materializeDeferredAliasTemplateArg(
	const ASTNode& arg_node,
	const InlineVector<ASTNode, 4>& template_parameters,
	const InlineVector<StringHandle, 4>& param_names,
	const std::vector<TemplateTypeArg>& template_args) {
	const auto find_param_index = [&](StringHandle param_name) -> std::optional<size_t> {
		for (size_t i = 0; i < param_names.size(); ++i) {
			if (param_names[i] == param_name) {
				return i;
			}
		}
		return std::nullopt;
	};
	const auto normalize_alias_param_arg = [&](size_t alias_param_idx, const TemplateTypeArg& source_arg) {
		TemplateTypeArg normalized = source_arg;
		if (alias_param_idx < template_parameters.size() &&
			template_parameters[alias_param_idx].is<TemplateParameterNode>()) {
			const auto& alias_param = template_parameters[alias_param_idx].as<TemplateParameterNode>();
			if (alias_param.kind() == TemplateParameterKind::NonType && !normalized.is_value) {
				normalized.is_value = true;
				normalized.is_dependent = normalized.is_dependent || normalized.dependent_name.isValid();
				if (alias_param.has_type() && alias_param.type_node().is<TypeSpecifierNode>()) {
					const auto& param_type = alias_param.type_node().as<TypeSpecifierNode>();
					normalized.type_index = param_type.type_index();
					normalized.setCategory(param_type.type());
				} else if (!normalized.type_index.is_valid()) {
					normalized.type_index = nativeTypeIndex(TypeCategory::Int);
					normalized.setCategory(TypeCategory::Int);
				}
			}
		}
		return normalized;
	};

	if (arg_node.is<TypeSpecifierNode>()) {
		const TypeSpecifierNode& arg_type = arg_node.as<TypeSpecifierNode>();
		Token arg_token = arg_type.token();
		if (arg_token.type() == Token::Type::Identifier) {
			if (auto alias_param_idx = find_param_index(arg_token.handle());
				alias_param_idx.has_value() && *alias_param_idx < template_args.size()) {
				return normalize_alias_param_arg(*alias_param_idx, template_args[*alias_param_idx]);
			}
		}
		return TemplateTypeArg(arg_type);
	}

	if (!arg_node.is<ExpressionNode>()) {
		return std::nullopt;
	}

	const ExpressionNode& arg_expr = arg_node.as<ExpressionNode>();
	if (const auto* tparam_ref = std::get_if<TemplateParameterReferenceNode>(&arg_expr)) {
		if (auto alias_param_idx = find_param_index(tparam_ref->param_name());
			alias_param_idx.has_value() && *alias_param_idx < template_args.size()) {
			return normalize_alias_param_arg(*alias_param_idx, template_args[*alias_param_idx]);
		}
		return TemplateTypeArg::makeDependentValue(tparam_ref->param_name(), TypeCategory::Int);
	}

	if (const auto* id = std::get_if<IdentifierNode>(&arg_expr)) {
		StringHandle id_handle = StringTable::getOrInternStringHandle(id->name());
		if (auto alias_param_idx = find_param_index(id_handle);
			alias_param_idx.has_value() && *alias_param_idx < template_args.size()) {
			return normalize_alias_param_arg(*alias_param_idx, template_args[*alias_param_idx]);
		}

		return TemplateTypeArg::makeDependentValue(id_handle, TypeCategory::Int);
	}

	ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
	auto eval_result = ConstExpr::Evaluator::evaluate(arg_node, eval_ctx);
	if (eval_result.success()) {
		return templateTypeArgFromEvalResult(eval_result);
	}

	std::vector<std::string_view> template_param_names_sv;
	template_param_names_sv.reserve(param_names.size());
	for (StringHandle param_name : param_names) {
		template_param_names_sv.push_back(StringTable::getStringView(param_name));
	}

	if (auto substituted_eval = substituteAndEvaluateNonTypeDefault(
			arg_node,
			template_parameters,
			std::span<const TemplateTypeArg>(template_args.data(), template_args.size()),
			std::span<const std::string_view>(template_param_names_sv.data(), template_param_names_sv.size()))) {
		return *substituted_eval;
	}

	if (const auto* qual_id = std::get_if<QualifiedIdentifierNode>(&arg_expr)) {
		return TemplateTypeArg::makeDependentValue(
			StringTable::getOrInternStringHandle(qual_id->full_name()),
			TypeCategory::Bool);
	}

	return std::nullopt;
}

std::optional<std::vector<TemplateTypeArg>> Parser::materializeDeferredAliasTemplateArgs(
	const TemplateAliasNode& alias_node,
	const std::vector<TemplateTypeArg>& template_args) {
	std::vector<TemplateTypeArg> substituted_args;
	const auto& param_names = alias_node.template_param_names();
	const auto& target_template_args = alias_node.target_template_args();
	substituted_args.reserve(target_template_args.size());

	for (const auto& arg_node : target_template_args) {
		auto materialized_arg = materializeDeferredAliasTemplateArg(
			arg_node,
			alias_node.template_parameters(),
			param_names,
			template_args);
		if (!materialized_arg.has_value()) {
			return std::nullopt;
		}
		substituted_args.push_back(std::move(*materialized_arg));
	}

	return substituted_args;
}

void Parser::normalizeDependentNonTypeTemplateArgs(
	const InlineVector<ASTNode, 4>& template_parameters,
	std::vector<TemplateTypeArg>& template_args) {
	size_t arg_index = 0;
	for (size_t param_index = 0;
		 param_index < template_parameters.size() && arg_index < template_args.size();
		 ++param_index) {
		if (!template_parameters[param_index].is<TemplateParameterNode>()) {
			continue;
		}

		const TemplateParameterNode& template_param =
			template_parameters[param_index].as<TemplateParameterNode>();
		if (template_param.is_variadic()) {
			break;
		}

		TemplateTypeArg& arg = template_args[arg_index];
		if (template_param.kind() == TemplateParameterKind::NonType &&
			arg.is_dependent &&
			!arg.is_value) {
			arg.is_value = true;
			arg.value = 0;
			TypeCategory value_category = TypeCategory::Int;
			if (template_param.has_type() && template_param.type_node().is<TypeSpecifierNode>()) {
				value_category = template_param.type_node().as<TypeSpecifierNode>().category();
			}
			TypeIndex value_type_index = nativeTypeIndex(value_category);
			arg.type_index = value_type_index.is_valid()
				? value_type_index
				: TypeIndex{0, value_category};
		}

		++arg_index;
	}
}

Parser::AliasTemplateMaterializationResult Parser::materializeAliasTemplateInstantiation(
	std::string_view alias_template_name,
	const std::vector<TemplateTypeArg>& template_args) {
	AliasTemplateMaterializationResult result;
	const TemplateAliasNode* alias_node = nullptr;
	if (auto alias_entry = gTemplateRegistry.lookup_alias_template(alias_template_name);
		alias_entry.has_value() && alias_entry->is<TemplateAliasNode>()) {
		alias_node = &alias_entry->as<TemplateAliasNode>();
	}
	std::string_view resolved_name = alias_template_name;
	result.instantiated_name = instantiate_and_register_base_template(resolved_name, template_args);
	if (result.instantiated_name.empty()) {
		return result;
	}

	result.resolved_type_info =
		findTypeByName(StringTable::getOrInternStringHandle(result.instantiated_name));
	if (alias_node != nullptr &&
		result.resolved_type_info != nullptr &&
		alias_template_name.find("::") != std::string_view::npos) {
		TypeIndex fallback_type_index =
			result.resolved_type_info->registeredTypeIndex().withCategory(
				result.resolved_type_info->typeEnum());
		if (const TypeInfo* concrete_member_alias =
				materializeInstantiatedMemberAliasTarget(
					alias_node->target_type_node(),
					fallback_type_index,
					alias_node->template_parameters(),
					template_args);
			concrete_member_alias != nullptr) {
			const TypeInfo* resolved_member_info = concrete_member_alias;
			ResolvedAliasTypeInfo resolved_member_alias = resolveAliasTypeInfo(
				concrete_member_alias->registeredTypeIndex().withCategory(
					concrete_member_alias->typeEnum()));
			if (resolved_member_alias.terminal_type_info != nullptr) {
				resolved_member_info = resolved_member_alias.terminal_type_info;
			}
			result.resolved_type_info = resolved_member_info;
			result.instantiated_name =
				StringTable::getStringView(resolved_member_info->name());
		}
	}
	return result;
}

Parser::AliasTemplateMaterializationResult Parser::materializeTemplateInstantiationForLookup(
	std::string_view template_name,
	const std::vector<TemplateTypeArg>& template_args) {
	if (gTemplateRegistry.lookup_alias_template(template_name).has_value()) {
		AliasTemplateMaterializationResult alias_result =
			materializeAliasTemplateInstantiation(template_name, template_args);
		if (!alias_result.instantiated_name.empty()) {
			normalizePendingSemanticRootsIfAvailable();
			if (alias_result.resolved_type_info == nullptr) {
				alias_result.resolved_type_info =
					findTypeByName(StringTable::getOrInternStringHandle(alias_result.instantiated_name));
			}
		}
		return alias_result;
	}

	AliasTemplateMaterializationResult result;
	std::string_view template_name_to_instantiate = template_name;
	result.instantiated_name =
		instantiate_and_register_base_template(template_name_to_instantiate, template_args);
	if (!result.instantiated_name.empty()) {
		normalizePendingSemanticRootsIfAvailable();
	} else {
		auto registry_hit = gTemplateRegistry.getInstantiation(
			StringTable::getOrInternStringHandle(template_name), template_args);
		if (registry_hit.has_value() && registry_hit->is<StructDeclarationNode>()) {
			result.instantiated_name = StringTable::getStringView(
				registry_hit->as<StructDeclarationNode>().name());
		} else {
			// get_instantiated_class_name always returns a non-empty mangled name,
			// so no further fallback is needed.
			result.instantiated_name =
				get_instantiated_class_name(template_name, template_args);
		}
	}

	if (!result.instantiated_name.empty()) {
		result.resolved_type_info =
			findTypeByName(StringTable::getOrInternStringHandle(result.instantiated_name));
	}
	return result;
}

const TypeInfo* Parser::materializeInstantiatedMemberAliasTarget(
	const TypeSpecifierNode& alias_type_spec,
	TypeIndex fallback_type_index,
	const InlineVector<ASTNode, 4>& template_params,
	const std::vector<TemplateTypeArg>& template_args) {
	const TypeInfo* original_alias_target_info = tryGetTypeInfo(alias_type_spec.type_index());
	if (!original_alias_target_info) {
		return nullptr;
	}

	std::string_view original_alias_target_name =
		StringTable::getStringView(original_alias_target_info->name());
	size_t member_sep = original_alias_target_name.rfind("::");
	if (member_sep == std::string_view::npos) {
		return nullptr;
	}

	std::string_view dependent_base_name =
		original_alias_target_name.substr(0, member_sep);
	std::string_view dependent_member_name =
		original_alias_target_name.substr(member_sep + 2);
	const TypeInfo* dependent_base_info = findTypeByName(
		StringTable::getOrInternStringHandle(dependent_base_name));
	if (!dependent_base_info || !dependent_base_info->isTemplateInstantiation()) {
		return nullptr;
	}

	std::string_view base_template_name =
		StringTable::getStringView(dependent_base_info->baseTemplateName());
	std::vector<TemplateTypeArg> concrete_base_args =
		materializeTemplateArgs(*dependent_base_info, template_params, template_args);
	AliasTemplateMaterializationResult materialized_alias_base =
		materializeTemplateInstantiationForLookup(
			base_template_name,
			concrete_base_args);
	if (materialized_alias_base.instantiated_name.empty()) {
		return nullptr;
	}

	StringHandle concrete_member_handle =
		StringTable::getOrInternStringHandle(
			StringBuilder()
				.append(materialized_alias_base.instantiated_name)
				.append("::")
				.append(dependent_member_name)
				.commit());
	auto concrete_member_it = getTypesByNameMap().find(concrete_member_handle);
	if (concrete_member_it != getTypesByNameMap().end() &&
		concrete_member_it->second != nullptr) {
		return concrete_member_it->second;
	}

	const TypeInfo* resolved_member_source = tryGetTypeInfo(fallback_type_index);
	if (!resolved_member_source) {
		return nullptr;
	}

	TypeIndex resolved_member_index =
		resolved_member_source->registeredTypeIndex().withCategory(
			resolved_member_source->typeEnum());
	TypeSpecifierNode concrete_member_spec(
		resolved_member_index,
		resolved_member_source->sizeInBits(),
		Token(),
		CVQualifier::None,
		ReferenceQualifier::None);
	if (const TypeSpecifierNode* existing_alias_spec =
			resolved_member_source->aliasTypeSpecifier()) {
		concrete_member_spec = *existing_alias_spec;
	}
	TypeInfo& concrete_member_info = add_type_alias_copy(
		concrete_member_handle,
		resolved_member_index,
		resolved_member_source->sizeInBits().value,
		concrete_member_spec);
	getTypesByNameMap().insert_or_assign(
		concrete_member_handle,
		&concrete_member_info);
	return &concrete_member_info;
}

bool Parser::resolveAliasTemplateInstantiation(
	TypeSpecifierNode& type_spec,
	std::string_view alias_template_name,
	const std::vector<TemplateTypeArg>& template_args) {
	AliasTemplateMaterializationResult materialized_alias =
		materializeAliasTemplateInstantiation(alias_template_name, template_args);
	if (!materialized_alias.resolved_type_info) {
		return false;
	}

	const TypeInfo* resolved_info = materialized_alias.resolved_type_info;

	type_spec.set_type_index(
		resolved_info->registeredTypeIndex().withCategory(resolved_info->typeEnum()));
	type_spec.set_category(resolved_info->typeEnum());
	type_spec.set_size_in_bits(resolved_info->sizeInBits());
	return true;
}

bool Parser::resolveAliasTemplateInstantiation(TypeSpecifierNode& type_spec) {
	const TypeInfo* aliased_info = tryGetTypeInfo(type_spec.type_index());
	if (!aliased_info || !aliased_info->isTemplateInstantiation()) {
		return false;
	}

	std::string_view base_template_name = StringTable::getStringView(aliased_info->baseTemplateName());
	if (!gTemplateRegistry.lookup_alias_template(base_template_name).has_value()) {
		return false;
	}

	std::vector<TemplateTypeArg> concrete_args;
	concrete_args.reserve(aliased_info->templateArgs().size());
	for (const auto& arg_info : aliased_info->templateArgs()) {
		concrete_args.push_back(toTemplateTypeArg(arg_info));
	}

	return resolveAliasTemplateInstantiation(type_spec, base_template_name, concrete_args);
}

// Helper function to instantiate base class template and register it in the AST
// This consolidates the duplicated code for instantiating base class templates
// Returns the instantiated name, or empty string_view if not a template
std::string_view Parser::instantiate_and_register_base_template(
	std::string_view& base_class_name,
	const std::vector<TemplateTypeArg>& template_args) {

	// First check if the base class is a template alias (like bool_constant)
	auto alias_entry = gTemplateRegistry.lookup_alias_template(base_class_name);
	if (alias_entry.has_value()) {
		FLASH_LOG(Parser, Debug, "Base class '", base_class_name, "' is a template alias - resolving");

		const TemplateAliasNode& alias_node = alias_entry->as<TemplateAliasNode>();

		if (alias_node.is_deferred()) {
			auto substituted_args_opt = materializeDeferredAliasTemplateArgs(alias_node, template_args);
			if (!substituted_args_opt.has_value()) {
				return std::string_view();
			}
			std::vector<TemplateTypeArg> substituted_args = std::move(*substituted_args_opt);

			// Now recursively instantiate the target template
			// The target might itself be a template alias (chain of aliases)
			std::string_view target_name(alias_node.target_template_name());
			std::string_view instantiated_name = instantiate_and_register_base_template(target_name, substituted_args);
			if (!instantiated_name.empty()) {
				base_class_name = instantiated_name;
				return instantiated_name;
			}
		}
	}

	// Check if the base class is a template class
	auto template_entry = gTemplateRegistry.lookupTemplate(base_class_name);
	if (template_entry) {
		// Try to instantiate the base template
		auto instantiated_base = try_instantiate_class_template(base_class_name, template_args);

		// If instantiation returned a struct node, add it to the AST so it gets visited during codegen
		// and get the actual instantiated name from the struct (which includes default arguments)
		if (instantiated_base.has_value() && instantiated_base->is<StructDeclarationNode>()) {
			registerAndNormalizeLateMaterializedTopLevelNode(*instantiated_base);
			// Get the actual instantiated name from the struct node (includes default args)
			StringHandle name_handle = instantiated_base->as<StructDeclarationNode>().name();
			std::string_view instantiated_name = StringTable::getStringView(name_handle);
			base_class_name = instantiated_name;
			return instantiated_name;
		}

		// If instantiation returned nullopt (already instantiated), look up the existing type
		// We need to fill in default arguments to find the correct name
		auto primary_template_opt = gTemplateRegistry.lookupTemplate(base_class_name);
		if (primary_template_opt.has_value() && primary_template_opt->is<TemplateClassDeclarationNode>()) {
			const TemplateClassDeclarationNode& primary_template = primary_template_opt->as<TemplateClassDeclarationNode>();
			const auto& primary_params = primary_template.template_parameters();
			const std::vector<std::string_view> primary_param_names =
				buildTemplateParamNames(primary_params);

			// Fill in defaults for missing arguments
			std::vector<TemplateTypeArg> filled_args = template_args;
			for (size_t i = filled_args.size(); i < primary_params.size(); ++i) {
				if (!primary_params[i].is<TemplateParameterNode>())
					continue;

				const TemplateParameterNode& param = primary_params[i].as<TemplateParameterNode>();
				if (param.is_variadic())
					continue;
				if (!param.has_default())
					break;

				const ASTNode& default_node = param.default_value();
				if (param.kind() == TemplateParameterKind::Type && default_node.is<TypeSpecifierNode>()) {
					InlineVector<TemplateTypeArg, 4> filled_args_inline =
						toInlineTemplateArgs(filled_args);
					ASTNode substituted_default_node = substituteTemplateParameters(
						default_node,
						primary_params,
						filled_args_inline);
					if (substituted_default_node.is<TypeSpecifierNode>()) {
						filled_args.emplace_back(substituted_default_node.as<TypeSpecifierNode>());
					} else {
						const TypeSpecifierNode& default_type = default_node.as<TypeSpecifierNode>();
						filled_args.emplace_back(default_type);
					}
					FLASH_LOG(Templates, Debug, "Filled in default type argument for param ", i);
				} else if (param.kind() == TemplateParameterKind::NonType && default_node.is<ExpressionNode>()) {
					if (auto evaluated_default = substituteAndEvaluateNonTypeDefault(
							default_node,
							primary_params,
							std::span<const TemplateTypeArg>(
								filled_args.data(),
								filled_args.size()),
							primary_param_names);
						evaluated_default.has_value()) {
						filled_args.push_back(*evaluated_default);
						FLASH_LOG(Templates, Debug, "Filled in default non-type argument for param ", i);
					}
				}
			}

			// Generate name with filled-in defaults
			std::string_view instantiated_name = get_instantiated_class_name(base_class_name, filled_args);
			base_class_name = instantiated_name;
			return instantiated_name;
		}

		// Fallback: use basic name without defaults
		std::string_view instantiated_name = get_instantiated_class_name(base_class_name, template_args);
		base_class_name = instantiated_name;
		return instantiated_name;
	}
	return std::string_view();
}

// Helper: resolve sizeof(member_alias_type) for a qualified owner.
// Looks up owner::type_name in the type registry, resolves the alias, and
// returns a concrete sizeof(resolved_type) AST node.  Returns nullopt if the
// lookup or resolution fails.
std::optional<ASTNode> Parser::tryResolveSizeofMemberAlias(
	StringHandle substitution_owner,
	std::string_view type_name,
	const Token& sizeof_token) {
	if (!substitution_owner.isValid() || type_name.empty()) {
		return std::nullopt;
	}
	StringHandle qualified_alias_name = StringTable::getOrInternStringHandle(
		StringBuilder()
			.append(substitution_owner)
			.append("::")
			.append(type_name)
			.commit());
	auto qualified_type_it = getTypesByNameMap().find(qualified_alias_name);
	if (qualified_type_it == getTypesByNameMap().end() || qualified_type_it->second == nullptr) {
		return std::nullopt;
	}
	const TypeInfo& qualified_type_info = *qualified_type_it->second;
	const ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(
		qualified_type_info.registeredTypeIndex().withCategory(qualified_type_info.typeEnum()));
	FLASH_LOG(Templates, Debug, "sizeof substitution: resolved member alias ", StringTable::getStringView(qualified_alias_name),
			  " size_bits=", qualified_type_info.sizeInBits().value);
	TypeSpecifierNode new_type(
		qualified_type_info.registeredTypeIndex().withCategory(qualified_type_info.typeEnum()),
		qualified_type_info.hasStoredSize() ? qualified_type_info.sizeInBits().value : 0,
		sizeof_token,
		CVQualifier::None,
		ReferenceQualifier::None);
	new_type.set_reference_qualifier(resolved_alias.reference_qualifier);
	for (size_t p = 0; p < resolved_alias.pointer_depth; ++p) {
		new_type.add_pointer_level(CVQualifier::None);
	}
	if (!resolved_alias.array_dimensions.empty()) {
		new_type.set_array_dimensions(resolved_alias.array_dimensions);
	}
	if (resolved_alias.function_signature.has_value()) {
		new_type.set_function_signature(*resolved_alias.function_signature);
	}
	auto new_type_node = emplace_node<TypeSpecifierNode>(new_type);
	SizeofExprNode new_sizeof(new_type_node, sizeof_token);
	return emplace_node<ExpressionNode>(new_sizeof);
}

// Helper function to substitute template parameters in an expression
// This recursively traverses the expression tree and replaces constructor calls with template parameter types
ASTNode Parser::substitute_template_params_in_expression(
	const ASTNode& expr,
	const std::unordered_map<TypeIndex, TemplateTypeArg>& type_substitution_map,
	const std::unordered_map<std::string_view, int64_t>& nontype_substitution_map,
	StringHandle substitution_owner) {

	// ASTNode is a typed pointer wrapper, check if it contains an ExpressionNode
	if (!expr.is<ExpressionNode>()) {
		FLASH_LOG(Templates, Debug, "substitute_template_params_in_expression: not an ExpressionNode");
		return expr; // Return as-is if not an expression
	}

	const ExpressionNode& expr_variant = expr.as<ExpressionNode>();
	FLASH_LOG(Templates, Debug, "substitute_template_params_in_expression: processing expression, variant index=", expr_variant.index());

	// Handle sizeof expressions
	if (std::holds_alternative<SizeofExprNode>(expr_variant)) {
		const SizeofExprNode& sizeof_node = std::get<SizeofExprNode>(expr_variant);

		// If sizeof has a type operand, check if it needs substitution
		if (sizeof_node.is_type() && sizeof_node.type_or_expr().is<TypeSpecifierNode>()) {
			const TypeSpecifierNode& type_node = sizeof_node.type_or_expr().as<TypeSpecifierNode>();

			FLASH_LOG(Templates, Debug, "sizeof substitution: checking type_index=", type_node.type_index(),
					  " type=", static_cast<int>(type_node.type()));

			// First, try to find by type_index
			auto it = type_substitution_map.find(type_node.type_index());
			if (it != type_substitution_map.end()) {
				FLASH_LOG(Templates, Debug, "sizeof substitution: FOUND match by type_index, substituting with ", it->second.toString());

				// Create a new type node with the substituted type
				const TemplateTypeArg& arg = it->second;
				TypeSpecifierNode new_type(
					arg.typeEnum(),
					TypeQualifier::None,
					get_type_size_bits(arg.category()),
					sizeof_node.sizeof_token(), CVQualifier::None);
				new_type.set_type_index(arg.type_index);

				// Apply cv-qualifiers, references, and pointers from template argument
				new_type.set_reference_qualifier(arg.ref_qualifier);
				for (size_t p = 0; p < arg.pointer_depth; ++p) {
					new_type.add_pointer_level(CVQualifier::None);
				}

				// Create new sizeof with substituted type
				auto new_type_node = emplace_node<TypeSpecifierNode>(new_type);
				SizeofExprNode new_sizeof(new_type_node, sizeof_node.sizeof_token());
				return emplace_node<ExpressionNode>(new_sizeof);
			}

			// If not found by type_index, try to find by matching type name with any substitution value
			// This handles the case where template parameter type_indices don't match due to
			// multiple template parameters with the same name in different templates
			if ((type_node.category() == TypeCategory::UserDefined || type_node.category() == TypeCategory::TypeAlias || type_node.category() == TypeCategory::Template)) {
				std::string_view type_name = type_node.token().value();
				if (type_name.empty()) {
					if (const TypeInfo* type_info = tryGetTypeInfo(type_node.type_index())) {
						type_name = StringTable::getStringView(type_info->name());
					}
				}
				if (!type_name.empty()) {
					FLASH_LOG(Templates, Debug, "sizeof substitution: checking by name: ", type_name);

					// Search substitution map for any entry where the key type_index has the same name
					for (const auto& [key_type_index, arg] : type_substitution_map) {
						if (const TypeInfo* key_type_info = tryGetTypeInfo(key_type_index)) {
							std::string_view param_name = StringTable::getStringView(key_type_info->name());
							if (param_name == type_name) {
								FLASH_LOG(Templates, Debug, "sizeof substitution: FOUND match by name, substituting with ", arg.toString());

							// Create a new type node with the substituted type
								TypeSpecifierNode new_type(
									arg.typeEnum(),
									TypeQualifier::None,
									get_type_size_bits(arg.category()),
									sizeof_node.sizeof_token(), CVQualifier::None);
								new_type.set_type_index(arg.type_index);

							// Apply cv-qualifiers, references, and pointers from template argument
								new_type.set_reference_qualifier(arg.ref_qualifier);
								for (size_t p = 0; p < arg.pointer_depth; ++p) {
									new_type.add_pointer_level(CVQualifier::None);
								}

							// Create new sizeof with substituted type
								auto new_type_node = emplace_node<TypeSpecifierNode>(new_type);
								SizeofExprNode new_sizeof(new_type_node, sizeof_node.sizeof_token());
								return emplace_node<ExpressionNode>(new_sizeof);
							}
						}
					}
				}
			}

			if (auto resolved = tryResolveSizeofMemberAlias(substitution_owner, type_node.token().value(), sizeof_node.sizeof_token())) {
				return *resolved;
			}

			FLASH_LOG(Templates, Debug, "sizeof substitution: NO match found");
		} else if (!sizeof_node.is_type()) {
			if (sizeof_node.type_or_expr().is<ExpressionNode>() &&
				std::holds_alternative<IdentifierNode>(sizeof_node.type_or_expr().as<ExpressionNode>())) {
				const IdentifierNode& id_node = std::get<IdentifierNode>(sizeof_node.type_or_expr().as<ExpressionNode>());
				if (auto resolved = tryResolveSizeofMemberAlias(substitution_owner, id_node.name(), sizeof_node.sizeof_token())) {
					return *resolved;
				}
			}
			// If sizeof has an expression operand, recursively substitute
			auto new_operand = substitute_template_params_in_expression(
				sizeof_node.type_or_expr(), type_substitution_map, nontype_substitution_map, substitution_owner);
			SizeofExprNode new_sizeof = SizeofExprNode::from_expression(new_operand, sizeof_node.sizeof_token());
			return emplace_node<ExpressionNode>(new_sizeof);
		}
	}

	// Handle identifiers that might be non-type template parameters
	if (std::holds_alternative<IdentifierNode>(expr_variant)) {
		const IdentifierNode& id_node = std::get<IdentifierNode>(expr_variant);
		std::string_view id_name = id_node.name();

		// Check if this identifier is a non-type template parameter
		auto it = nontype_substitution_map.find(id_name);
		if (it != nontype_substitution_map.end()) {
			// Replace the identifier with a numeric literal
			int64_t value = it->second;
			// Create a persistent string for the token value using StringBuilder
			std::string_view val_str = StringBuilder().append(value).commit();
			Token value_token(Token::Type::Literal, val_str, 0, 0, 0);
			return emplace_node<ExpressionNode>(
				NumericLiteralNode(value_token, static_cast<unsigned long long>(value), TypeCategory::Int, TypeQualifier::None, 32));
		}
	}

	// Handle constructor call: T(value) -> ConcreteType(value)
	if (std::holds_alternative<ConstructorCallNode>(expr_variant)) {
		const ConstructorCallNode& ctor = std::get<ConstructorCallNode>(expr_variant);
		const TypeSpecifierNode& ctor_type = ctor.type_node().as<TypeSpecifierNode>();

		// Check if this constructor type is in our substitution map
		// For variable templates with cleaned-up template parameters, the constructor
		// might have type_index=0 or some other invalid value. So we check if there's
		// exactly one entry in the map and assume any UserDefined constructor is for that type.
		if (ctor_type.category() == TypeCategory::UserDefined || ctor_type.category() == TypeCategory::TypeAlias || ctor_type.category() == TypeCategory::Template) {
			// If we have exactly one type substitution and this is a UserDefined constructor,
			// assume it's for the template parameter
			if (type_substitution_map.size() == 1) {
				const TemplateTypeArg& arg = type_substitution_map.begin()->second;

				// Create a new type specifier with the concrete type
				TypeSpecifierNode new_type(
					arg.typeEnum(),
					TypeQualifier::None,
					get_type_size_bits(arg.category()),
					ctor.called_from(), CVQualifier::None);

				// Recursively substitute in arguments
				ChunkedVector<ASTNode> new_args;
				for (size_t i = 0; i < ctor.arguments().size(); ++i) {
					new_args.push_back(substitute_template_params_in_expression(ctor.arguments()[i], type_substitution_map, nontype_substitution_map, substitution_owner));
				}

				// Create new constructor call with substituted type
				auto new_type_node = emplace_node<TypeSpecifierNode>(new_type);
				ConstructorCallNode new_ctor(new_type_node, std::move(new_args), ctor.called_from());
				return emplace_node<ExpressionNode>(new_ctor);
			}
		}

		// Not a template parameter constructor - recursively substitute in arguments
		ChunkedVector<ASTNode> new_args;
		for (size_t i = 0; i < ctor.arguments().size(); ++i) {
			new_args.push_back(substitute_template_params_in_expression(ctor.arguments()[i], type_substitution_map, nontype_substitution_map, substitution_owner));
		}
		ConstructorCallNode new_ctor(ctor.type_node(), std::move(new_args), ctor.called_from());
		return emplace_node<ExpressionNode>(new_ctor);
	}

	// Handle binary operators - recursively substitute in both operands
	if (std::holds_alternative<BinaryOperatorNode>(expr_variant)) {
		const BinaryOperatorNode& binop = std::get<BinaryOperatorNode>(expr_variant);
		auto new_left = substitute_template_params_in_expression(
			binop.get_lhs(), type_substitution_map, nontype_substitution_map, substitution_owner);
		auto new_right = substitute_template_params_in_expression(
			binop.get_rhs(), type_substitution_map, nontype_substitution_map, substitution_owner);

		BinaryOperatorNode new_binop(
			binop.get_token(),
			new_left,
			new_right);
		return emplace_node<ExpressionNode>(new_binop);
	}

	// Handle unary operators - recursively substitute in operand
	if (std::holds_alternative<UnaryOperatorNode>(expr_variant)) {
		const UnaryOperatorNode& unop = std::get<UnaryOperatorNode>(expr_variant);

		// Special case: sizeof with a type operand that needs substitution
		// For example: sizeof(T) where T is a template parameter
		if (unop.op() == "sizeof" && unop.get_operand().is<TypeSpecifierNode>()) {
			const TypeSpecifierNode& type_node = unop.get_operand().as<TypeSpecifierNode>();

			FLASH_LOG(Templates, Debug, "sizeof substitution: checking type_index=", type_node.type_index(),
					  " type=", static_cast<int>(type_node.type()));

			// Check if this type needs substitution
			auto it = type_substitution_map.find(type_node.type_index());
			if (it != type_substitution_map.end()) {
				FLASH_LOG(Templates, Debug, "sizeof substitution: FOUND match, substituting with ", it->second.toString());

				// Create a new type node with the substituted type
				const TemplateTypeArg& arg = it->second;
				TypeSpecifierNode new_type(
					arg.typeEnum(),
					TypeQualifier::None,
					get_type_size_bits(arg.category()),
					unop.get_token(), CVQualifier::None);
				// Apply cv-qualifiers, references, and pointers from template argument
				new_type.set_reference_qualifier(arg.ref_qualifier);
				for (size_t p = 0; p < arg.pointer_depth; ++p) {
					new_type.add_pointer_level(CVQualifier::None);
				}

				// Create new sizeof with substituted type
				auto new_type_node = emplace_node<TypeSpecifierNode>(new_type);
				UnaryOperatorNode new_unop(
					unop.get_token(),
					new_type_node,
					unop.is_prefix());
				return emplace_node<ExpressionNode>(new_unop);
			} else {
				FLASH_LOG(Templates, Debug, "sizeof substitution: NO match found in map");
			}
		}

		// General case: recursively substitute in operand
		auto new_operand = substitute_template_params_in_expression(
			unop.get_operand(), type_substitution_map, nontype_substitution_map, substitution_owner);

		UnaryOperatorNode new_unop(
			unop.get_token(),
			new_operand,
			unop.is_prefix());
		return emplace_node<ExpressionNode>(new_unop);
	}

	// Handle qualified identifiers (e.g., SomeTemplate<T>::member)
	// Phase 3: For variable templates that reference class template static members,
	// substitution is intentionally deferred to try_instantiate_variable_template() because:
	// 1. The namespace component contains the mangled name with template parameters
	// 2. We don't have enough context here to re-parse and instantiate the template
	// 3. The type_substitution_map only contains type indices, not the full template arguments
	// The actual template instantiation happens in try_instantiate_variable_template() which has
	// access to concrete template arguments and can trigger proper specialization pattern matching.

	// For all expression types (including QualifiedIdentifierNode), return as-is
	return expr;
}

// Try to instantiate a class template with explicit template arguments
// Returns the instantiated StructDeclarationNode if successful
// Try to instantiate a variable template with the given template arguments
// Returns the instantiated variable declaration node or nullopt if already instantiated
std::optional<ASTNode> Parser::try_instantiate_variable_template(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args) {
	// First, try to find a partial specialization that matches the template arguments
	// For example, is_reference_v<int&> should match is_reference_v<T&>
	// Pattern names are: template_name_R (lvalue ref), template_name_RR (rvalue ref), template_name_P (pointer)

	// Extract simple name from template_name (remove namespace prefix if present)
	std::string_view simple_template_name = template_name;
	size_t last_colon_pos = template_name.rfind("::");
	if (last_colon_pos != std::string_view::npos) {
		simple_template_name = template_name.substr(last_colon_pos + 2);
	}

	FLASH_LOG(Templates, Debug, "try_instantiate_variable_template: template_name='", template_name,
			  "' simple_name='", simple_template_name, "' args.size()=", template_args.size());

	// Build resolved args list — apply template_param_substitutions_ to all args once
	// Do this BEFORE the dependency check so that dependent args that have substitutions
	// available (e.g., _R1 -> ratio<1,2>) get resolved first.
	std::vector<TemplateTypeArg> resolved_args;
	resolved_args.reserve(template_args.size());
	for (const auto& original_arg : template_args) {
		TemplateTypeArg arg = original_arg;
		if (arg.is_dependent && arg.dependent_name.isValid()) {
			// Try to resolve dependent arg using template_param_substitutions_
			StringHandle dep_name = arg.dependent_name;
			for (const auto& subst : template_param_substitutions_) {
				if (subst.is_type_param && subst.param_name == dep_name && !subst.substituted_type.is_dependent) {
					FLASH_LOG(Templates, Debug, "Resolving dependent template parameter '", dep_name,
							  "' with concrete type ", subst.substituted_type.toString());
					arg = subst.substituted_type;
					break;
				}
			}
		}
		if (!arg.is_dependent && arg.type_index.is_valid()) {
			if (const TypeInfo* type_info = tryGetTypeInfo(arg.type_index)) {
				StringHandle type_name = type_info->name();
				for (const auto& subst : template_param_substitutions_) {
					if (subst.is_type_param && subst.param_name == type_name && !subst.substituted_type.is_dependent) {
						FLASH_LOG(Templates, Debug, "Substituting template parameter '", type_name,
								  "' with concrete type ", subst.substituted_type.toString());
						arg = subst.substituted_type;
						break;
					}
				}
			}
		}
		resolved_args.push_back(arg);
	}

	// Check if any template argument is still dependent after substitution
	// If so, we cannot instantiate - this happens when we're inside a template body
	for (size_t i = 0; i < resolved_args.size(); ++i) {
		const auto& arg = resolved_args[i];
		if (arg.is_dependent) {
			FLASH_LOG(Templates, Debug, "Skipping variable template '", template_name,
					  "' instantiation - arg[", i, "] is dependent: ", arg.toString());
			return std::nullopt;
		}
	}

	auto template_opt = gTemplateRegistry.lookupVariableTemplate(template_name);
	if (!template_opt.has_value() && template_name != simple_template_name) {
		template_opt = gTemplateRegistry.lookupVariableTemplate(simple_template_name);
	}
	if (!template_opt.has_value()) {
		FLASH_LOG(Templates, Error, "Variable template '", template_name, "' not found");
		return std::nullopt;
	}

	if (!template_opt->is<TemplateVariableDeclarationNode>()) {
		FLASH_LOG(Templates, Error, "Expected TemplateVariableDeclarationNode");
		return std::nullopt;
	}

	const TemplateVariableDeclarationNode& var_template = template_opt->as<TemplateVariableDeclarationNode>();
	const auto& template_params = var_template.template_parameters();

	auto fill_missing_variable_template_args =
		[&](const std::vector<TemplateTypeArg>& input_args) -> std::optional<std::vector<TemplateTypeArg>> {
		bool has_parameter_pack = false;
		size_t non_variadic_param_count = 0;
		for (const auto& param_node : template_params) {
			if (!param_node.is<TemplateParameterNode>()) {
				continue;
			}

			const auto& param = param_node.as<TemplateParameterNode>();
			if (param.is_variadic()) {
				has_parameter_pack = true;
				continue;
			}
			++non_variadic_param_count;
		}

		if (has_parameter_pack) {
			size_t minimum_required_args = 0;
			for (const auto& param_node : template_params) {
				if (!param_node.is<TemplateParameterNode>()) {
					continue;
				}

				const auto& param = param_node.as<TemplateParameterNode>();
				if (param.is_variadic() || param.has_default()) {
					continue;
				}
				++minimum_required_args;
			}

			if (input_args.size() < minimum_required_args) {
				FLASH_LOG(Templates, Error, "Too few arguments for variadic variable template '",
						  template_name, "' (got ", input_args.size(), ", need at least ", minimum_required_args, ")");
				return std::nullopt;
			}
		} else if (input_args.size() > non_variadic_param_count) {
			FLASH_LOG(Templates, Error, "Too many arguments for variable template '",
					  template_name, "' (got ", input_args.size(), ", max ", non_variadic_param_count, ")");
			return std::nullopt;
		}

		auto materialize_default_arg =
			[&](const TemplateParameterNode& param, const std::vector<TemplateTypeArg>& bound_args) -> std::optional<TemplateTypeArg> {
			if (!param.has_default()) {
				return std::nullopt;
			}

			const ASTNode& default_node = param.default_value();
			InlineVector<TemplateTypeArg, 4> bound_args_inline = toInlineTemplateArgs(bound_args);
			ASTNode substituted_default = substituteTemplateParameters(default_node, template_params, bound_args_inline);

			if (param.kind() == TemplateParameterKind::Type) {
				if (substituted_default.is<TypeSpecifierNode>()) {
					return TemplateTypeArg(substituted_default.as<TypeSpecifierNode>());
				}
				FLASH_LOG(Templates, Error, "Failed to materialize type default for variable template parameter '",
						  param.name(), "'");
				return std::nullopt;
			}

			if (param.kind() == TemplateParameterKind::NonType) {
				if (!substituted_default.is<ExpressionNode>()) {
					FLASH_LOG(Templates, Error, "Failed to substitute non-type default for variable template parameter '",
							  param.name(), "'");
					return std::nullopt;
				}

				ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
				auto eval_result = ConstExpr::Evaluator::evaluate(substituted_default, eval_ctx);
				if (!eval_result.success()) {
					FLASH_LOG(Templates, Error, "Failed to evaluate non-type default for variable template parameter '",
							  param.name(), "'");
					return std::nullopt;
				}

				return templateTypeArgFromEvalResult(eval_result);
			}

			FLASH_LOG(Templates, Error, "Unsupported variable template parameter kind for default argument on '",
					  param.name(), "'");
			return std::nullopt;
		};

		std::vector<TemplateTypeArg> filled_args;
		filled_args.reserve(std::max(input_args.size(), template_params.size()));
		size_t arg_index = 0;

		for (size_t i = 0; i < template_params.size(); ++i) {
			if (!template_params[i].is<TemplateParameterNode>()) {
				continue;
			}

			const auto& param = template_params[i].as<TemplateParameterNode>();
			if (param.is_variadic()) {
				size_t remaining_args = arg_index < input_args.size()
					? input_args.size() - arg_index
					: 0;
				size_t required_after = countRequiredTemplateArgsAfter<InlineVector<ASTNode, 4>, std::vector<TemplateTypeArg>>(
					template_params, i + 1);
				size_t pack_size = remaining_args > required_after
					? remaining_args - required_after
					: 0;
				for (size_t pack_index = 0; pack_index < pack_size; ++pack_index) {
					filled_args.push_back(input_args[arg_index + pack_index]);
				}
				arg_index += pack_size;
				continue;
			}

			if (arg_index < input_args.size()) {
				filled_args.push_back(input_args[arg_index]);
				++arg_index;
				continue;
			}

			auto default_arg = materialize_default_arg(param, filled_args);
			if (!default_arg.has_value()) {
				FLASH_LOG(Templates, Error, "Variable template '", template_name,
						  "': missing argument for parameter '", param.name(), "'");
				return std::nullopt;
			}

			filled_args.push_back(*default_arg);
		}

		if (arg_index != input_args.size()) {
			FLASH_LOG(Templates, Error, "Too many arguments for variable template '",
					  template_name, "' after canonical binding (consumed ", arg_index,
					  " of ", input_args.size(), ")");
			return std::nullopt;
		}

		return filled_args;
	};

	auto filled_args_opt = fill_missing_variable_template_args(resolved_args);
	if (!filled_args_opt.has_value()) {
		return std::nullopt;
	}
	const std::vector<TemplateTypeArg>& filled_args = *filled_args_opt;

	// Structural pattern matching: find the best matching partial specialization
	// Uses TemplatePattern::matches() which handles qualifier matching, multi-arg,
	// and proper template parameter deduction without string-based pattern keys.
	auto structural_match = gTemplateRegistry.findVariableTemplateSpecialization(simple_template_name, filled_args);
	// Also try qualified name if simple name didn't match
	if (!structural_match.has_value() && template_name != simple_template_name) {
		structural_match = gTemplateRegistry.findVariableTemplateSpecialization(template_name, filled_args);
	}

	if (structural_match.has_value() && structural_match->node.is<TemplateVariableDeclarationNode>()) {
		FLASH_LOG(Templates, Debug, "Found variable template partial specialization via structural match");
		const TemplateVariableDeclarationNode& spec_template = structural_match->node.as<TemplateVariableDeclarationNode>();
		const VariableDeclarationNode& spec_var_decl = spec_template.variable_decl_node();
		const Token& orig_token = spec_var_decl.declaration().identifier_token();
		std::string_view persistent_name = FlashCpp::generateInstantiatedNameFromArgs(simple_template_name, filled_args);

		if (gSymbolTable.lookup(persistent_name).has_value()) {
			return gSymbolTable.lookup(persistent_name);
		}

		const DeclarationNode& spec_decl = spec_var_decl.declaration();
		ASTNode spec_type = spec_decl.type_node();
		const auto& spec_params = spec_template.template_parameters();
		std::vector<TemplateTypeArg> converted_args;
		if (!spec_params.empty()) {
			// Build deduced args from the structural match substitutions.
			// TemplatePattern::matches() already deduced T→int by stripping
			// pattern qualifiers, so we use those substitutions directly.
			converted_args.reserve(spec_params.size());
			for (const auto& param : spec_params) {
				if (param.is<TemplateParameterNode>()) {
					const TemplateParameterNode& tp = param.as<TemplateParameterNode>();
					auto it = structural_match->substitutions.find(tp.nameHandle());
					if (it != structural_match->substitutions.end()) {
						converted_args.push_back(it->second);
					} else {
						// Fallback: use resolved arg with qualifiers stripped
						if (converted_args.size() < filled_args.size()) {
							FLASH_LOG(Templates, Debug, "Deduction fallback for param '",
									  tp.name(), "': using arg[", converted_args.size(), "] with qualifiers stripped");
							TemplateTypeArg deduced = filled_args[converted_args.size()];
							deduced.ref_qualifier = ReferenceQualifier::None;
							deduced.pointer_depth = 0;
							deduced.pointer_cv_qualifiers.clear();
							deduced.is_array = false;
							converted_args.push_back(deduced);
						} else {
							FLASH_LOG(Templates, Warning, "Cannot deduce param '",
									  tp.name(), "': no substitution and no remaining args");
						}
					}
				}
			}
		}

		std::optional<ASTNode> init_expr;
		if (spec_var_decl.initializer().has_value()) {
			if (!spec_params.empty()) {
				init_expr = substituteTemplateParameters(
					*spec_var_decl.initializer(), spec_params, converted_args);
				spec_type = substituteTemplateParameters(
					spec_type, spec_params, converted_args);
			} else {
				init_expr = *spec_var_decl.initializer();
			}
		} else if (spec_decl.type_node().is<TypeSpecifierNode>() &&
				   spec_decl.type_node().as<TypeSpecifierNode>().category() == TypeCategory::Bool) {
			Token true_token(Token::Type::Keyword, "true"sv, orig_token.line(), orig_token.column(), orig_token.file_index());
			init_expr = emplace_node<ExpressionNode>(BoolLiteralNode(true_token, true));
		}

		auto decl_node = emplace_node<DeclarationNode>(spec_type,
													   Token(Token::Type::Identifier, persistent_name, orig_token.line(), orig_token.column(), orig_token.file_index()));

		auto var_decl_node = emplace_node<VariableDeclarationNode>(decl_node, init_expr, StorageClass::None);
		var_decl_node.as<VariableDeclarationNode>().set_is_constexpr(true);
		setOuterTemplateBindingsFromParams(var_decl_node.as<VariableDeclarationNode>(), spec_params, converted_args);
		gSymbolTable.insertGlobal(persistent_name, var_decl_node);
		registerAndNormalizeLateMaterializedTopLevelNodeFront(var_decl_node);
		return var_decl_node;
	}

	// Generate unique name for the instantiation using hash-based naming
	// This ensures consistent naming with class template instantiations
	std::string_view persistent_name = FlashCpp::generateInstantiatedNameFromArgs(simple_template_name, filled_args);

	// Check if already instantiated
	if (gSymbolTable.lookup(persistent_name).has_value()) {
		return gSymbolTable.lookup(persistent_name);
	}

	// Get the original variable declaration
	const VariableDeclarationNode& orig_var_decl = var_template.variable_decl_node();
	const DeclarationNode& orig_decl = orig_var_decl.declaration();
	InlineVector<TemplateTypeArg, 4> filled_args_inline = toInlineTemplateArgs(filled_args);
	ASTNode substituted_type = substituteTemplateParameters(orig_decl.type_node(), template_params, filled_args_inline);

	// Create new declaration with substituted type and instantiated name
	// Use original token's line/column/file info for better diagnostics
	const Token& orig_token = orig_decl.identifier_token();
	Token instantiated_name_token(Token::Type::Identifier, persistent_name, orig_token.line(), orig_token.column(), orig_token.file_index());
	auto new_decl_node = emplace_node<DeclarationNode>(substituted_type, instantiated_name_token);

	// Substitute template parameters in initializer expression
	std::optional<ASTNode> new_initializer = std::nullopt;
	if (orig_var_decl.initializer().has_value()) {
		FLASH_LOG(Templates, Debug, "Substituting initializer expression for variable template");
		new_initializer = substituteTemplateParameters(
			orig_var_decl.initializer().value(),
			template_params,
			filled_args_inline);
		FLASH_LOG(Templates, Debug, "Initializer substitution complete");

		// PHASE 3 FIX: After substitution, trigger instantiation of any class templates
		// referenced in the initializer expression. This ensures specialization pattern
		// matching happens before codegen.
		// For example: is_pointer_v<int*> = is_pointer_impl<int*>::value
		// After substitution, we need to instantiate is_pointer_impl<int*> which should
		// match the specialization pattern is_pointer_impl<T*> and inherit from true_type.
		if (new_initializer.has_value()) {
			FLASH_LOG(Templates, Debug, "Phase 3: Checking initializer for variable template '", template_name,
					  "', is ExpressionNode: ", new_initializer->is<ExpressionNode>());

			if (new_initializer->is<ExpressionNode>()) {
				const ExpressionNode& init_expr = new_initializer->as<ExpressionNode>();

				// Check if the initializer is a qualified identifier (e.g., Template<Args>::member)
				bool is_qual_id = std::holds_alternative<QualifiedIdentifierNode>(init_expr);
				FLASH_LOG(Templates, Debug, "Phase 3: Is QualifiedIdentifierNode: ", is_qual_id);

				if (is_qual_id) {
					const QualifiedIdentifierNode& qual_id = std::get<QualifiedIdentifierNode>(init_expr);

					// The struct/class name is the namespace handle's name
					// For "is_pointer_impl<int*>::value", the namespace name is "is_pointer_impl<int*>"
					NamespaceHandle ns_handle = qual_id.namespace_handle();
					FLASH_LOG(Templates, Debug, "Phase 3: Namespace handle depth: ", gNamespaceRegistry.getDepth(ns_handle));

					if (!ns_handle.isGlobal()) {
						// Get the struct name from the namespace handle
						std::string_view struct_name_view = gNamespaceRegistry.getName(ns_handle);

						FLASH_LOG(Templates, Debug, "Phase 3: Struct name from qualified ID: '", struct_name_view, "'");

						// The struct name might be a mangled template instantiation (hash-based)
						// Extract the base template name from metadata
						std::string_view template_name_to_lookup = struct_name_view;
						std::string_view base_name = extractBaseTemplateName(struct_name_view);
						if (!base_name.empty()) {
							template_name_to_lookup = base_name;
							FLASH_LOG(Templates, Debug, "Phase 3: Extracted template name: '", template_name_to_lookup, "'");
						}

						// Try to instantiate the struct/class referenced in the qualified identifier
						// Look it up to see if it's a template
						auto inner_template_opt = gTemplateRegistry.lookupTemplate(template_name_to_lookup);
						if (inner_template_opt.has_value()) {
							std::vector<TemplateTypeArg> inner_template_args = resolved_args;
							auto inner_type_it = getTypesByNameMap().find(
								StringTable::getOrInternStringHandle(struct_name_view));
							if (inner_type_it != getTypesByNameMap().end() &&
								inner_type_it->second != nullptr &&
								inner_type_it->second->isTemplateInstantiation()) {
								inner_template_args = materializePlaceholderTemplateArgs(
									*inner_type_it->second,
									template_params,
									filled_args);
							}

							// This is a template - try to instantiate it with the concrete arguments
							// Use the referenced placeholder's own template arguments so outer
							// defaults are only forwarded when the inner template actually uses them.
							FLASH_LOG(Templates, Debug, "Phase 3: Triggering instantiation of '", template_name_to_lookup,
									  "' with ", inner_template_args.size(), " args from variable template initializer");

							auto instantiated = try_instantiate_class_template(template_name_to_lookup, inner_template_args);
							if (instantiated.has_value() && instantiated->is<StructDeclarationNode>()) {
								// Add to AST so it gets codegen
								registerAndNormalizeLateMaterializedTopLevelNode(*instantiated);

								// Now update the qualified identifier to use the correct instantiated name
								// Get the instantiated class name (e.g., "is_pointer_impl_intP")
								std::string_view instantiated_name = get_instantiated_class_name(template_name_to_lookup, inner_template_args);
								FLASH_LOG(Templates, Debug, "Phase 3: Instantiated class name: '", instantiated_name, "'");

								// Create a new qualified identifier with the updated namespace
								// Get the parent namespace and add the instantiated name as a child
								NamespaceHandle parent_ns = gNamespaceRegistry.getParent(ns_handle);
								StringHandle instantiated_name_handle = StringTable::getOrInternStringHandle(instantiated_name);
								NamespaceHandle new_ns_handle = gNamespaceRegistry.getOrCreateNamespace(parent_ns, instantiated_name_handle);

								// Create new qualified identifier node
								QualifiedIdentifierNode new_qual_id(new_ns_handle, qual_id.identifier_token());
								new_initializer = emplace_node<ExpressionNode>(new_qual_id);

								FLASH_LOG(Templates, Debug, "Phase 3: Successfully instantiated and updated qualifier in variable template initializer");
							}
						}
					}
				}
			}
		}
	}

	// Create instantiated variable declaration
	auto instantiated_var_decl = emplace_node<VariableDeclarationNode>(
		new_decl_node,
		new_initializer,
		orig_var_decl.storage_class());
	// Mark as constexpr to match the template pattern
	instantiated_var_decl.as<VariableDeclarationNode>().set_is_thread_local(orig_var_decl.is_thread_local());
	instantiated_var_decl.as<VariableDeclarationNode>().set_is_constexpr(true);
	setOuterTemplateBindingsFromParams(instantiated_var_decl.as<VariableDeclarationNode>(), template_params, filled_args);

	// Register the VariableDeclarationNode in symbol table (not just DeclarationNode)
	// This allows constexpr evaluation to find and evaluate the variable
	// IMPORTANT: Use insertGlobal because we might be called during function parsing
	// but we need to insert into global scope
	[[maybe_unused]] bool insert_result = gSymbolTable.insertGlobal(persistent_name, instantiated_var_decl);

	// Add to AST at the beginning so it gets code-generated before functions that use it
	// Insert after other global declarations but before function definitions
	registerAndNormalizeLateMaterializedTopLevelNodeFront(instantiated_var_decl);

	return instantiated_var_decl;
}

// Helper to instantiate a full template specialization (e.g., template<> struct Tuple<> {})
std::optional<ASTNode> Parser::instantiate_full_specialization(
	std::string_view template_name,
	const std::vector<TemplateTypeArg>& template_args,
	ASTNode& spec_node) {
	// Generate the instantiated class name
	std::string_view instantiated_name = get_instantiated_class_name(template_name, template_args);
	FLASH_LOG(Templates, Debug, "instantiate_full_specialization called for: ", instantiated_name);

	if (!spec_node.is<StructDeclarationNode>()) {
		FLASH_LOG(Templates, Error, "Full specialization is not a StructDeclarationNode");
		return std::nullopt;
	}

	StructDeclarationNode& spec_struct = spec_node.as<StructDeclarationNode>();
	InlineVector<ASTNode, 4> no_template_params;

	// Helper lambda to register type aliases with qualified names
	auto register_type_aliases = [&]() {
		auto resolveConcreteSiblingAlias = [&](const TypeSpecifierNode& alias_type_spec) -> const TypeInfo* {
			if (alias_type_spec.token().type() == Token::Type::Identifier &&
				!alias_type_spec.token().value().empty()) {
				StringHandle direct_sibling_handle = StringTable::getOrInternStringHandle(
					StringBuilder()
						.append(instantiated_name)
						.append("::")
						.append(alias_type_spec.token().value())
						.commit());
				auto direct_sibling_it = getTypesByNameMap().find(direct_sibling_handle);
				if (direct_sibling_it != getTypesByNameMap().end() &&
					direct_sibling_it->second != nullptr) {
					return direct_sibling_it->second;
				}
			}

			const TypeInfo* alias_target_info = tryGetTypeInfo(alias_type_spec.type_index());
			if (alias_target_info == nullptr) {
				return nullptr;
			}

			std::string_view alias_target_name =
				StringTable::getStringView(alias_target_info->name());
			size_t scope_pos = alias_target_name.rfind("::");
			if (scope_pos == std::string_view::npos) {
				return nullptr;
			}

			StringHandle sibling_handle = StringTable::getOrInternStringHandle(
				StringBuilder()
					.append(instantiated_name)
					.append("::")
					.append(alias_target_name.substr(scope_pos + 2))
					.commit());
			auto sibling_it = getTypesByNameMap().find(sibling_handle);
			if (sibling_it == getTypesByNameMap().end()) {
				return nullptr;
			}
			return sibling_it->second;
		};

		for (const auto& type_alias : spec_struct.type_aliases()) {
			// Build the qualified name using StringBuilder
			StringHandle qualified_alias_name = StringTable::getOrInternStringHandle(StringBuilder()
																						 .append(instantiated_name)
																						 .append("::")
																						 .append(type_alias.alias_name));

			// Get the type information from the alias
			const TypeSpecifierNode& alias_type_spec = type_alias.type_node.as<TypeSpecifierNode>();
			TypeIndex alias_target_index = alias_type_spec.type_index();
			int alias_size_bits = alias_type_spec.size_in_bits();
			TypeSpecifierNode alias_registration_type_spec = alias_type_spec;
			if (const TypeInfo* concrete_sibling_alias =
					resolveConcreteSiblingAlias(alias_type_spec);
				concrete_sibling_alias != nullptr) {
				alias_target_index =
					concrete_sibling_alias->registeredTypeIndex().withCategory(
						concrete_sibling_alias->typeEnum());
				alias_size_bits = concrete_sibling_alias->sizeInBits().value;
				if (const TypeSpecifierNode* concrete_alias_spec =
						concrete_sibling_alias->aliasTypeSpecifier()) {
					alias_registration_type_spec = *concrete_alias_spec;
				} else {
					alias_registration_type_spec.set_type_index(alias_target_index);
					alias_registration_type_spec.set_category(
						concrete_sibling_alias->typeEnum());
					alias_registration_type_spec.set_size_in_bits(
						concrete_sibling_alias->sizeInBits());
				}
			}
			if (const TypeInfo* concrete_member_info =
					materializeInstantiatedMemberAliasTarget(
						alias_type_spec,
						alias_target_index,
						no_template_params,
						template_args);
				concrete_member_info != nullptr) {
				alias_target_index =
					concrete_member_info->registeredTypeIndex().withCategory(
						concrete_member_info->typeEnum());
				alias_size_bits = concrete_member_info->sizeInBits().value;
				if (const TypeSpecifierNode* concrete_alias_spec =
						concrete_member_info->aliasTypeSpecifier()) {
					alias_registration_type_spec = *concrete_alias_spec;
				} else {
					alias_registration_type_spec.set_type_index(alias_target_index);
					alias_registration_type_spec.set_category(concrete_member_info->typeEnum());
					alias_registration_type_spec.set_size_in_bits(
						concrete_member_info->sizeInBits());
				}
			}

			// Register the type alias globally with its qualified name
			TypeInfo* alias_info = nullptr;
			auto existing_it = getTypesByNameMap().find(qualified_alias_name);
			if (existing_it != getTypesByNameMap().end() && existing_it->second != nullptr) {
				alias_info = existing_it->second;
				const uint32_t alias_slot = alias_info->registeredTypeIndex().index();
				alias_info->type_index_ = alias_target_index;
				alias_info->registered_type_index_ = TypeIndex{alias_slot, TypeCategory::TypeAlias};
				alias_info->fallback_size_bits_ = alias_size_bits;
				alias_info->is_type_alias_ = true;
				alias_info->clearAliasTypeSpecifier();
				alias_info->setAliasTypeSpecifier(alias_registration_type_spec);
			} else {
				alias_info = &add_type_alias_copy(
					qualified_alias_name,
					alias_target_index,
					alias_size_bits,
					alias_registration_type_spec);
			}
			TypeInfo& alias_type_info = *alias_info;
			if (alias_registration_type_spec.category() == TypeCategory::Enum) {
				if (const TypeInfo* source_alias_type_info = tryGetTypeInfo(alias_target_index);
					source_alias_type_info && source_alias_type_info->getEnumInfo()) {
					const EnumTypeInfo* enum_info = source_alias_type_info->getEnumInfo();
					alias_type_info.setEnumInfo(std::make_unique<EnumTypeInfo>(*enum_info));
				}
			}
			getTypesByNameMap().insert_or_assign(qualified_alias_name, &alias_type_info);

			FLASH_LOG(Templates, Debug, "Registered type alias: ", StringTable::getStringView(qualified_alias_name),
					  " -> type=", static_cast<int>(alias_registration_type_spec.type()),
					  ", type_index=", alias_target_index);
		}
	};
	auto register_nested_class_aliases = [&]() {
		for (const auto& nested_class : spec_struct.nested_classes()) {
			if (!nested_class.is<StructDeclarationNode>()) {
				continue;
			}

			const StructDeclarationNode& nested_struct =
				nested_class.as<StructDeclarationNode>();
			std::string_view original_nested_name = StringBuilder()
				.append(StringTable::getStringView(spec_struct.name()))
				.append("::")
				.append(nested_struct.name())
				.commit();
			auto original_nested_it = getTypesByNameMap().find(
				StringTable::getOrInternStringHandle(original_nested_name));
			if (original_nested_it == getTypesByNameMap().end() ||
				original_nested_it->second == nullptr) {
				continue;
			}

			const TypeInfo* original_nested_info = original_nested_it->second;
			StringHandle qualified_nested_name = StringTable::getOrInternStringHandle(
				StringBuilder()
					.append(instantiated_name)
					.append("::")
					.append(nested_struct.name())
					.commit());
			if (getTypesByNameMap().find(qualified_nested_name) ==
				getTypesByNameMap().end()) {
				TypeIndex nested_target_index =
					original_nested_info->registeredTypeIndex().withCategory(
						original_nested_info->typeEnum());
				TypeSpecifierNode nested_alias_spec(
					nested_target_index,
					original_nested_info->sizeInBits(),
					Token(),
					CVQualifier::None,
					ReferenceQualifier::None);
				TypeInfo& nested_alias_info = add_type_alias_copy(
					qualified_nested_name,
					nested_target_index,
					original_nested_info->sizeInBits().value,
					nested_alias_spec);
				getTypesByNameMap().insert_or_assign(
					qualified_nested_name,
					&nested_alias_info);
			}

			for (const auto& type_alias : nested_struct.type_aliases()) {
				StringHandle qualified_alias_name = StringTable::getOrInternStringHandle(
					StringBuilder()
						.append(StringTable::getStringView(qualified_nested_name))
						.append("::")
						.append(type_alias.alias_name)
						.commit());
				const TypeSpecifierNode& alias_type_spec =
					type_alias.type_node.as<TypeSpecifierNode>();
				TypeIndex alias_target_index = alias_type_spec.type_index();
				TypeSpecifierNode alias_registration_type_spec = alias_type_spec;
				if (const TypeInfo* alias_target_info = tryGetTypeInfo(alias_target_index);
					alias_target_info != nullptr &&
					StringTable::getStringView(alias_target_info->name()) ==
						original_nested_name) {
					alias_target_index =
						original_nested_info->registeredTypeIndex().withCategory(
							original_nested_info->typeEnum());
					alias_registration_type_spec.set_type_index(alias_target_index);
					alias_registration_type_spec.set_category(
						original_nested_info->typeEnum());
					alias_registration_type_spec.set_size_in_bits(
						original_nested_info->sizeInBits());
				}

				TypeInfo& alias_type_info = add_type_alias_copy(
					qualified_alias_name,
					alias_target_index,
					alias_registration_type_spec.size_in_bits(),
					alias_registration_type_spec);
				getTypesByNameMap().insert_or_assign(
					qualified_alias_name,
					&alias_type_info);
			}
		}
	};

	// Check if we already have this instantiation
	auto existing_type = getTypesByNameMap().find(StringTable::getOrInternStringHandle(instantiated_name));
	if (existing_type != getTypesByNameMap().end()) {
		FLASH_LOG(Templates, Debug, "Full spec already instantiated: ", instantiated_name);

		// Even if the struct is already instantiated, we need to register type aliases
		// with qualified names if they haven't been registered yet
		register_type_aliases();
		register_nested_class_aliases();

		return std::nullopt;	 // Already instantiated
	}

	FLASH_LOG(Templates, Debug, "Instantiating full specialization: ", instantiated_name);

	// Resolve the namespace where the template was DECLARED, not where it's being instantiated.
	NamespaceHandle decl_ns = gSymbolTable.get_current_namespace_handle();
	{
		if (template_name.find("::") != std::string_view::npos) {
			decl_ns = QualifiedIdentifier::fromQualifiedName(template_name, NamespaceRegistry::GLOBAL_NAMESPACE).namespace_handle;
		} else {
			std::string_view decl_name = StringTable::getStringView(spec_struct.name());
			if (size_t pos = decl_name.rfind("::"); pos != std::string_view::npos) {
				decl_ns = QualifiedIdentifier::fromQualifiedName(decl_name, NamespaceRegistry::GLOBAL_NAMESPACE).namespace_handle;
			} else {
				// Neither template_name nor spec_struct.name() contains "::".
				// Look up the template's registered TypeInfo to get its declaration-site
				// NamespaceHandle. This handles global-scope full specializations
				// (e.g., template<> struct Foo<int> {}) instantiated from a non-global namespace.
				auto tmpl_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(template_name));
				if (tmpl_it != getTypesByNameMap().end()) {
					decl_ns = tmpl_it->second->namespaceHandle();
				}
			}
		}
	}

	// Create TypeInfo for the specialization
	TypeInfo& struct_type_info = add_struct_type(StringTable::getOrInternStringHandle(instantiated_name), decl_ns);

	// Store template instantiation metadata for O(1) lookup (Phase 6)
	auto template_args_info = convertToTemplateArgInfo(template_args);
	struct_type_info.setTemplateInstantiationInfo(
		QualifiedIdentifier::fromQualifiedName(template_name, decl_ns),
		template_args_info);
	struct_type_info.setInstantiationContext({}, template_args_info, nullptr);

	auto struct_info = std::make_unique<StructTypeInfo>(StringTable::getOrInternStringHandle(instantiated_name), spec_struct.default_access(), spec_struct.is_union(), decl_ns);

	// Copy members from the specialization
	for (const auto& member_decl : spec_struct.members()) {
		const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
		const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();

		TypeCategory member_type = type_spec.type();
		TypeIndex member_type_index = type_spec.type_index();
		size_t ptr_depth = type_spec.pointer_depth();

		size_t member_size;
		if (ptr_depth > 0 || type_spec.is_reference() || type_spec.is_rvalue_reference()) {
			member_size = 8;
		} else {
			member_size = get_type_size_bits(member_type) / 8;
		}
		size_t member_alignment = get_type_alignment(member_type, member_size);

		// Phase 7B: Intern member name and use StringHandle overload
		StringHandle member_name_handle = decl.identifier_token().handle();
		struct_info->addMember(
			member_name_handle,
			member_type_index,
			member_size,
			member_alignment,
			member_decl.access,
			member_decl.default_initializer,
			type_spec.reference_qualifier(),
			type_spec.reference_qualifier() != ReferenceQualifier::None ? get_type_size_bits(member_type) : 0,
			false,
			{},
			static_cast<int>(type_spec.pointer_depth()),
			member_decl.bitfield_width,
			type_spec.has_function_signature() ? std::optional(type_spec.function_signature()) : std::nullopt,
			member_decl.is_no_unique_address);
	}

	// Copy static members. Prefer the specialization AST so we preserve in-class
	// initializers even when the parsed StructTypeInfo has not materialized them yet.
	if (!spec_struct.static_members().empty()) {
		for (const auto& static_member : spec_struct.static_members()) {
			FLASH_LOG(Templates, Debug, "Copying static member: ", StringTable::getStringView(static_member.name));
			struct_info->addStaticMember(
				static_member.name,
				static_member.type_index,
				static_member.size,
				static_member.alignment,
				static_member.access,
				static_member.initializer,
				static_member.cv_qualifier,
				static_member.reference_qualifier,
				static_member.pointer_depth,
				static_member.is_array,
				static_member.array_dimensions);
		}
	} else {
		// Fall back to the specialization's StructTypeInfo when the AST does not
		// carry static members (older registration paths).
		auto spec_name_lookup = spec_struct.name();
		auto spec_type_it = getTypesByNameMap().find(spec_name_lookup);
		if (spec_type_it != getTypesByNameMap().end()) {
			const StructTypeInfo* spec_struct_info = spec_type_it->second->getStructInfo();
			if (spec_struct_info) {
				for (const auto& static_member : spec_struct_info->static_members) {
					FLASH_LOG(Templates, Debug, "Copying static member: ", static_member.getName());
					struct_info->static_members.push_back(static_member);
				}
			}
		}
	}

	// Copy type aliases from the specialization
	// Type aliases need to be registered with qualified names (e.g., "MyType_bool::type")
	register_type_aliases();
	register_nested_class_aliases();

	// Check if there's an explicit constructor - if not, we need to generate a default one
	bool has_constructor = false;
	for (auto& mem_func : spec_struct.member_functions()) {
		if (mem_func.is_constructor) {
			has_constructor = true;

			// Handle constructor - it's a ConstructorDeclarationNode
			const ConstructorDeclarationNode& orig_ctor = mem_func.function_declaration.as<ConstructorDeclarationNode>();

			// Create a NEW ConstructorDeclarationNode with the instantiated struct name
			auto [new_ctor_node, new_ctor_ref] = emplace_node_ref<ConstructorDeclarationNode>(
				StringTable::getOrInternStringHandle(instantiated_name),	 // Set correct parent struct name
				orig_ctor.name()	 // Constructor name
			);

			// Copy parameters
			for (const auto& param : orig_ctor.parameter_nodes()) {
				new_ctor_ref.add_parameter_node(param);
			}

			// Copy member initializers
			for (const auto& [name, expr] : orig_ctor.member_initializers()) {
				new_ctor_ref.add_member_initializer(name, expr);
			}

			// Copy definition if present
			if (orig_ctor.is_materialized()) {
				new_ctor_ref.set_definition(*orig_ctor.get_definition());
			}

			// Add the constructor to struct_info
			struct_info->addConstructor(new_ctor_node, mem_func.access);

			// Add to AST for code generation
			registerLateMaterializedTopLevelNode(new_ctor_node);
		} else if (mem_func.is_destructor) {
			// Handle destructor - create new node with correct struct name
			const DestructorDeclarationNode& orig_dtor = mem_func.function_declaration.as<DestructorDeclarationNode>();

			auto [new_dtor_node, new_dtor_ref] = emplace_node_ref<DestructorDeclarationNode>(
				StringTable::getOrInternStringHandle(instantiated_name),
				orig_dtor.name());

			// Copy noexcept properties from the original destructor declaration.
			// DestructorDeclarationNode defaults to noexcept(true) per C++11, so we
			// must propagate the original's evaluated flag (and expression, if any)
			// to handle explicit noexcept(false) correctly.
			new_dtor_ref.set_noexcept(orig_dtor.is_noexcept());
			new_dtor_ref.set_has_noexcept_specifier(orig_dtor.has_noexcept_specifier());
			if (orig_dtor.has_noexcept_expression()) {
				new_dtor_ref.set_noexcept_expression(*orig_dtor.noexcept_expression());
			}

			// Copy definition if present
			if (orig_dtor.is_materialized()) {
				new_dtor_ref.set_definition(*orig_dtor.get_definition());
			}

			struct_info->addDestructor(new_dtor_node, mem_func.access, mem_func.is_virtual);
			registerLateMaterializedTopLevelNode(new_dtor_node);
		} else {
			FunctionDeclarationNode& orig_func = mem_func.function_declaration.as<FunctionDeclarationNode>();

			// Create a NEW FunctionDeclarationNode with the instantiated struct name
			auto new_func_node = emplace_node<FunctionDeclarationNode>(
				orig_func.decl_node(),
				instantiated_name);

			// Copy all parameters and definition
			FunctionDeclarationNode& new_func = new_func_node.as<FunctionDeclarationNode>();
			for (const auto& param : orig_func.parameter_nodes()) {
				new_func.add_parameter_node(param);
			}
			copy_function_properties(new_func, orig_func);
			if (orig_func.is_materialized()) {
				new_func.set_definition(*orig_func.get_definition());
			}

			// Phase 7B: Intern function name and use StringHandle overload
			StringHandle func_name_handle = orig_func.decl_node().identifier_token().handle();
			struct_info->addMemberFunction(
				func_name_handle,
				new_func_node,
				mem_func.access,
				mem_func.is_virtual,
				mem_func.is_pure_virtual,
				mem_func.is_override,
				mem_func.is_final);

			if (new_func.is_materialized()) {
				finalize_function_after_definition(new_func);
			} else {
				compute_and_set_mangled_name(new_func);
			}

			// Add to AST for code generation
			registerLateMaterializedTopLevelNode(new_func_node);
		}
	}
	normalizePendingSemanticRootsIfAvailable();

	// If no constructor was defined, we should synthesize a default one
	// For now, mark that we need one and it will be generated in codegen
	struct_info->needs_default_constructor = !has_constructor;
	FLASH_LOG(Templates, Debug, "Full spec has constructor: ", has_constructor ? "yes" : "no, needs default");

	struct_type_info.setStructInfo(std::move(struct_info));
	if (struct_type_info.getStructInfo()) {
		struct_type_info.fallback_size_bits_ = struct_type_info.getStructInfo()->sizeInBits().value;
	}

	return std::nullopt;	 // Return nullopt since we don't need to add anything to AST
}

// Helper function to substitute non-type template parameters in initializers
// Extracted from try_instantiate_class_template to reduce function size
std::optional<ASTNode> Parser::substitute_nontype_template_param(
	std::string_view param_name,
	const std::vector<TemplateTypeArg>& args,
	const std::vector<ASTNode>& params) {
	for (size_t i = 0; i < params.size(); ++i) {
		const TemplateParameterNode& tparam = params[i].as<TemplateParameterNode>();
		if (tparam.name() == param_name && tparam.kind() == TemplateParameterKind::NonType) {
			if (i < args.size() && args[i].is_value) {
				int64_t val = args[i].value;
				TypeCategory val_type = args[i].typeEnum();
				StringBuilder value_str;
				value_str.append(val);
				std::string_view value_view = value_str.commit();
				Token num_token(Token::Type::Literal, value_view, 0, 0, 0);
				return emplace_node<ExpressionNode>(
					NumericLiteralNode(num_token,
									   static_cast<unsigned long long>(val),
									   val_type,
									   TypeQualifier::None,
									   get_type_size_bits(val_type)));
			}
		}
	}
	return std::nullopt;
}

// Helper function to fill in default template arguments before pattern matching
// This is critical for SFINAE patterns like void_t

// Evaluates a dependent NTTP expression (e.g., sizeof(T), alignof(T)) with concrete template arguments.
// Delegates to substitute_template_params_in_expression then ConstExpr::Evaluator for correctness
// with struct types and complex expressions.
// Returns the evaluated value if successful, or nullopt if evaluation fails.
std::optional<int64_t> Parser::evaluateDependentNTTPExpression(
	const ASTNode& dependent_expr,
	std::span<const ASTNode> template_params,
	std::span<const TemplateTypeArg> template_args) {

	// Build type substitution map from template params to args
	std::unordered_map<TypeIndex, TemplateTypeArg> type_substitution_map;
	// Build non-type substitution map for value parameters
	std::unordered_map<std::string_view, int64_t> nontype_substitution_map;
	for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
		if (!template_params[i].is<TemplateParameterNode>()) {
			continue;
		}
		const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
		if (param.kind() == TemplateParameterKind::Type) {
			// For typename/class parameters, registered_type_index() is the TypeIndex assigned
			// when the template was parsed (via add_user_type in Parser_Templates_Function.cpp).
			// This is the same TypeIndex that sizeof(T) will carry in its TypeSpecifierNode.
			if (param.registered_type_index().is_valid()) {
				type_substitution_map[param.registered_type_index()] = template_args[i];
			}
			// For non-type parameters that have an explicit type node (e.g., template<int N>
			// where someone uses sizeof(N)), also map by the param's type specifier index.
			if (param.has_type() && param.type_node().is<TypeSpecifierNode>()) {
				const TypeSpecifierNode& param_type = param.type_node().as<TypeSpecifierNode>();
				type_substitution_map[param_type.type_index()] = template_args[i];
			}
			// Name-based fallback: look up by param name in the type registry.
			auto type_it = getTypesByNameMap().find(param.nameHandle());
			if (type_it != getTypesByNameMap().end() && type_it->second != nullptr) {
				type_substitution_map[type_it->second->type_index_] = template_args[i];
			}
		} else if (param.kind() == TemplateParameterKind::NonType && template_args[i].is_value) {
			nontype_substitution_map[param.name()] = template_args[i].value;
		}
	}

	// Additional pass: for sizeof/alignof expressions, directly match the type name
	// against template parameter names.  This handles class template type parameters
	// where registered_type_index() was not set by add_template_param_type and the
	// TypeInfo was removed from getTypesByNameMap() after template parsing.
	// We map the TypeIndex that appears inside sizeof(T) directly, which the
	// substitute_template_params_in_expression type_index lookup will then find.
	// TODO: The root cause is that Parser_Templates_Class.cpp does not call
	// tparam.set_registered_type_index() after add_template_param_type(), unlike
	// Parser_Templates_Function.cpp which does.  Fixing that would eliminate this
	// fallback.  See also: the RAII TemplateParameterScope removes the TypeInfo from
	// getTypesByNameMap() after parsing, making name-based lookup unreliable here.
	auto tryMapSizeofTypeByName = [&](const TypeSpecifierNode& type_node) {
		if (type_substitution_map.count(type_node.type_index())) {
			return;  // Already mapped via registered_type_index or getTypesByNameMap
		}
		std::string_view token_name = type_node.token().value();
		if (token_name.empty()) {
			if (const TypeInfo* ti = tryGetTypeInfo(type_node.type_index())) {
				token_name = StringTable::getStringView(ti->name());
			}
		}
		if (token_name.empty()) {
			return;
		}
		for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
			if (!template_params[i].is<TemplateParameterNode>()) {
				continue;
			}
			const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
			if (param.kind() == TemplateParameterKind::Type && param.name() == token_name) {
				type_substitution_map[type_node.type_index()] = template_args[i];
				break;
			}
		}
	};
	if (dependent_expr.is<ExpressionNode>()) {
		const ExpressionNode& top_variant = dependent_expr.as<ExpressionNode>();
		if (std::holds_alternative<SizeofExprNode>(top_variant)) {
			const SizeofExprNode& sn = std::get<SizeofExprNode>(top_variant);
			if (sn.is_type() && sn.type_or_expr().is<TypeSpecifierNode>()) {
				tryMapSizeofTypeByName(sn.type_or_expr().as<TypeSpecifierNode>());
			}
		} else if (std::holds_alternative<AlignofExprNode>(top_variant)) {
			const AlignofExprNode& an = std::get<AlignofExprNode>(top_variant);
			if (an.is_type() && an.type_or_expr().is<TypeSpecifierNode>()) {
				tryMapSizeofTypeByName(an.type_or_expr().as<TypeSpecifierNode>());
			}
		}
	}

	// Substitute template parameters in the expression to get a concrete AST
	ASTNode substituted = substitute_template_params_in_expression(
		dependent_expr, type_substitution_map, nontype_substitution_map, StringHandle{});

	// Evaluate the substituted expression using the standard constant expression evaluator
	ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
	eval_ctx.parser = this;
	ConstExpr::EvalResult result = ConstExpr::Evaluator::evaluate(substituted, eval_ctx);
	if (result.success()) {
		return static_cast<int64_t>(result.as_int());
	}

	FLASH_LOG(Templates, Debug, "evaluateDependentNTTPExpression: evaluation failed for dependent expression");
	return std::nullopt;
}
