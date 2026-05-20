#include "Parser.h"
#include "ConstExprEvaluator.h"
#include <span>
#include "ExpressionSubstitutor.h"
#include "NameMangling.h"
#include "OverloadResolution.h"
#include "TypeTraitEvaluator.h"

static void buildVariableTemplateParameterReplayState(
	std::span<const TemplateParameterNode> template_params,
	InlineVector<StringHandle, 4>& template_param_names,
	InlineVector<TemplateParameterKind, 4>& template_param_kinds,
	InlineVector<TypeCategory, 4>& non_type_categories) {
	template_param_names.clear();
	template_param_kinds.clear();
	non_type_categories.clear();
	template_param_names.reserve(template_params.size());
	template_param_kinds.reserve(template_params.size());
	non_type_categories.reserve(template_params.size());
	for (const TemplateParameterNode& template_param : template_params) {
		template_param_names.push_back(template_param.nameHandle());
		template_param_kinds.push_back(template_param.kind());
		non_type_categories.push_back(
			template_param.kind() == TemplateParameterKind::NonType &&
				template_param.has_type()
				? template_param.type_specifier_node().type()
				: TypeCategory::Invalid);
	}
}

TemplateTypeArg templateTypeArgFromEvalResult(const ConstExpr::EvalResult& eval_result) {
	TypeIndex value_type_index = eval_result.exact_type.has_value()
		? eval_result.exact_type->type_index().withCategory(eval_result.exact_type->category())
		: TypeIndex{};
	if (eval_result.exact_type.has_value() && eval_result.exact_type->category() == TypeCategory::Nullptr) {
		return TemplateTypeArg::makeValueIdentity(
			FlashCpp::NonTypeValueIdentity::makeNullptr(nativeTypeIndex(TypeCategory::Nullptr)));
	}
	if (eval_result.pointer_to_var.isValid()) {
		if (!value_type_index.is_valid()) {
			value_type_index = nativeTypeIndex(TypeCategory::Int);
		}
		FlashCpp::NonTypeValueIdentity identity = FlashCpp::NonTypeValueIdentity::makeObjectPointer(
			value_type_index,
			eval_result.pointer_to_var,
			eval_result.pointer_offset);
		TypeCategory value_category = value_type_index.category();
		if (eval_result.exact_type.has_value() &&
			eval_result.exact_type->is_reference()) {
			identity.kind = FlashCpp::NonTypeValueIdentityKind::Reference;
		} else if (value_category == TypeCategory::FunctionPointer ||
				   value_category == TypeCategory::MemberFunctionPointer) {
			identity.kind = FlashCpp::NonTypeValueIdentityKind::FunctionPointer;
		}
		return TemplateTypeArg::makeValueIdentity(identity);
	}
	if (eval_result.member_pointer_member.isValid() || eval_result.is_null_member_pointer) {
		if (!value_type_index.is_valid()) {
			value_type_index = nativeTypeIndex(TypeCategory::MemberObjectPointer);
		}
		StringHandle member_class_name{};
		if (eval_result.exact_type.has_value() && eval_result.exact_type->has_member_class()) {
			member_class_name = eval_result.exact_type->member_class_name();
		}
		return TemplateTypeArg::makeValueIdentity(
			FlashCpp::NonTypeValueIdentity::makeMemberPointer(
				value_type_index,
				eval_result.member_pointer_member,
				eval_result.as_int(),
				member_class_name));
	}
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

static int64_t convertIntegralTemplateValueToType(int64_t value, TypeCategory target_category) {
	switch (target_category) {
	case TypeCategory::Bool:
		return value != 0 ? 1 : 0;
	case TypeCategory::Char:
		return static_cast<int64_t>(static_cast<char>(value));
	case TypeCategory::UnsignedChar:
		return static_cast<int64_t>(static_cast<unsigned char>(value));
	case TypeCategory::WChar:
		return static_cast<int64_t>(static_cast<wchar_t>(value));
	case TypeCategory::Char8:
		return static_cast<int64_t>(static_cast<char8_t>(value));
	case TypeCategory::Char16:
		return static_cast<int64_t>(static_cast<char16_t>(value));
	case TypeCategory::Char32:
		return static_cast<int64_t>(static_cast<char32_t>(value));
	case TypeCategory::Short:
		return static_cast<int64_t>(static_cast<short>(value));
	case TypeCategory::UnsignedShort:
		return static_cast<int64_t>(static_cast<unsigned short>(value));
	case TypeCategory::Int:
		return static_cast<int64_t>(static_cast<int>(value));
	case TypeCategory::UnsignedInt:
		return static_cast<int64_t>(static_cast<unsigned int>(value));
	case TypeCategory::Long:
		return static_cast<int64_t>(static_cast<long>(value));
	case TypeCategory::UnsignedLong:
		return static_cast<int64_t>(static_cast<unsigned long>(value));
	case TypeCategory::LongLong:
		return static_cast<int64_t>(static_cast<long long>(value));
	case TypeCategory::UnsignedLongLong:
		return static_cast<int64_t>(static_cast<unsigned long long>(value));
	default:
		return value;
	}
}

TemplateTypeArg templateTypeArgFromEvalResult(
	const ConstExpr::EvalResult& eval_result,
	const TypeSpecifierNode& target_type) {
	TemplateTypeArg arg = templateTypeArgFromEvalResult(eval_result);
	if (!arg.is_value || arg.has_typed_value_identity) {
		return arg;
	}

	const TypeCategory target_category = target_type.type();
	if (target_category == TypeCategory::Invalid ||
		isPlaceholderAutoType(target_category) ||
		target_type.pointer_depth() != 0 ||
		target_type.reference_qualifier() != ReferenceQualifier::None ||
		target_type.has_function_signature() ||
		target_type.has_member_class()) {
		return arg;
	}
	if (!isIntegralType(target_category) && target_category != TypeCategory::Enum) {
		return arg;
	}

	TypeIndex target_type_index = target_type.type_index().withCategory(target_type.type());
	if (!target_type_index.is_valid() && is_builtin_type(target_type.type())) {
		target_type_index = nativeTypeIndex(target_type.type());
	}
	if (target_category == TypeCategory::Enum && !target_type_index.is_valid()) {
		return arg;
	}
	if (!target_type_index.is_valid()) {
		target_type_index = TypeIndex{0, target_category};
	}
	if (isIntegralType(target_category)) {
		arg.value = convertIntegralTemplateValueToType(arg.value, target_category);
	}
	arg.type_index = TemplateTypeArg::makeTypeIndex(target_type_index);
	return arg;
}

namespace {

	InlineVector<TemplateParameterNode, 4> getTargetTemplateParameters(StringHandle target_template_name) {
		if (!target_template_name.isValid()) {
			return {};
		}
		if (auto alias_template_opt = gTemplateRegistry.lookup_alias_template(target_template_name);
			alias_template_opt.has_value()) {
			return alias_template_opt->as<TemplateAliasNode>().template_parameters();
		}
		if (auto class_or_function_template_opt = gTemplateRegistry.lookupTemplate(target_template_name);
			class_or_function_template_opt.has_value()) {
			if (class_or_function_template_opt->is<TemplateClassDeclarationNode>()) {
				return class_or_function_template_opt->as<TemplateClassDeclarationNode>().template_parameters();
			}
			if (class_or_function_template_opt->is<TemplateFunctionDeclarationNode>()) {
				return class_or_function_template_opt->as<TemplateFunctionDeclarationNode>().template_parameters();
			}
		}
		if (auto variable_template_opt = gTemplateRegistry.lookupVariableTemplate(target_template_name);
			variable_template_opt.has_value()) {
			return variable_template_opt->as<TemplateVariableDeclarationNode>().template_parameters();
		}
		return {};
	}

	StringHandle getQualifiedIdentifierHandle(const QualifiedIdentifierNode& qual_id) {
		if (!qual_id.namespace_handle().isValid() || qual_id.namespace_handle().isGlobal()) {
			return qual_id.nameHandle();
		}
		return gNamespaceRegistry.buildQualifiedIdentifier(qual_id.namespace_handle(), qual_id.nameHandle());
	}

	std::optional<TemplateTypeArg> classifyDeferredQualifiedIdentifier(
		const QualifiedIdentifierNode& qual_id,
		const TemplateParameterNode* target_template_param) {
		if (target_template_param == nullptr) {
			return std::nullopt;
		}
		const StringHandle qualified_name = getQualifiedIdentifierHandle(qual_id);
		switch (target_template_param->kind()) {
		case TemplateParameterKind::NonType:
			if (!target_template_param->has_type()) {
				throw InternalError("Non-type target template parameter is missing a declared type");
			}
			return TemplateTypeArg::makeDependentValue(
				qualified_name,
				target_template_param->type_specifier_node().type());
		case TemplateParameterKind::Template:
			return TemplateTypeArg::makeTemplate(qualified_name);
		case TemplateParameterKind::Type:
			return std::nullopt;
		}
		return std::nullopt;
	}

	// Extract pack name from a pack-expansion pattern expression.
	// Supports identifier and template-parameter-reference patterns.
	std::optional<std::string_view> tryExtractPackNameFromPackExpansionPattern(const ASTNode& pattern) {
		if (!pattern.is<ExpressionNode>()) {
			return std::nullopt;
		}
		const ExpressionNode& pattern_expr = pattern.as<ExpressionNode>();
		if (const auto* pattern_id = std::get_if<IdentifierNode>(&pattern_expr)) {
			return pattern_id->name();
		}
		if (const auto* pattern_tparam = std::get_if<TemplateParameterReferenceNode>(&pattern_expr)) {
			return StringTable::getStringView(pattern_tparam->param_name());
		}
		return std::nullopt;
	}

	// Detect whether an alias target argument forwards a parameter pack and return the pack name.
	// Supports type-side `Type...` and expression-side `expr...` forwarding patterns.
	// TODO(template-pack-forwarding): Extend detection/materialization to qualified or computed
	// pack patterns (e.g. `const Ts...`, `(Vs + 1)...`) once deferred alias expansion supports them.
	std::optional<std::string_view> tryGetAliasPackForwardingName(const ASTNode& target_arg_node) {
		if (target_arg_node.is<TypeSpecifierNode>()) {
			const TypeSpecifierNode& target_arg_type = target_arg_node.as<TypeSpecifierNode>();
			if (target_arg_type.is_pack_expansion() &&
				target_arg_type.token().type() == Token::Type::Identifier) {
				return target_arg_type.token().value();
			}
			return std::nullopt;
		}
		if (!target_arg_node.is<ExpressionNode>()) {
			return std::nullopt;
		}
		const ExpressionNode& target_arg_expr = target_arg_node.as<ExpressionNode>();
		if (const auto* pack_expansion = std::get_if<PackExpansionExprNode>(&target_arg_expr)) {
			return tryExtractPackNameFromPackExpansionPattern(pack_expansion->pattern());
		}
		return std::nullopt;
	}

	ASTNode substituteNonTypeDefaultExpressionImpl(
		Parser& parser,
		const ASTNode& default_node,
		const InlineVector<TemplateParameterNode, 4>& template_params,
		std::span<const TemplateTypeArg> template_args) {
	if (!default_node.is<ExpressionNode>() || template_args.empty()) {
		return default_node;
	}

	auto sub_map = buildSubstitutionParamMap(template_params, template_args);
	TemplateInstantiationContext substitution_context = buildTemplateInstantiationContext(
		std::span<const TemplateParameterNode>(template_params.data(), template_params.size()),
		template_args,
		nullptr,
		TemplateSubstitutionFailurePolicy::HardUse);
	TemplateEnvironment& substitution_environment = substitution_context.environment;
	sub_map = buildSubstitutionParamMap(substitution_environment);
	if (sub_map.empty()) {
		return default_node;
	}

	ExpressionSubstitutor substitutor(substitution_context, parser);
	return substitutor.substitute(default_node);
}

	std::optional<TypeSpecifierNode> substituteNonTypeParameterTypeImpl(
		Parser& parser,
		const TemplateParameterNode& param,
		const InlineVector<TemplateParameterNode, 4>& template_params,
		std::span<const TemplateTypeArg> template_args) {
	if (param.kind() != TemplateParameterKind::NonType || !param.has_type()) {
		return std::nullopt;
	}
	ASTNode substituted_type_node = parser.substituteTemplateParameters(
		ASTNode::emplace_node<TypeSpecifierNode>(param.type_specifier_node()),
		template_params,
		template_args);
	if (!substituted_type_node.is<TypeSpecifierNode>()) {
		return std::nullopt;
	}
	return substituted_type_node.as<TypeSpecifierNode>();
}

	std::optional<TemplateTypeArg> substituteAndEvaluateNonTypeDefaultImpl(
		Parser& parser,
		const ASTNode& default_node,
		const InlineVector<TemplateParameterNode, 4>& template_params,
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

	ConstExpr::EvaluationContext eval_ctx(gSymbolTable, parser);
	eval_ctx.template_environment = buildTemplateEnvironment(
		std::span<const TemplateParameterNode>(template_params.data(), template_params.size()),
		template_args,
		nullptr);
	eval_ctx.template_args.assign(template_args.begin(), template_args.end());
	eval_ctx.template_param_names.assign(
		template_param_names.begin(),
		template_param_names.end());

	auto eval_result = ConstExpr::Evaluator::evaluate(substituted_default_node, eval_ctx);
	if (!eval_result.success()) {
		FLASH_LOG(Templates, Debug, "substituteAndEvaluateNonTypeDefaultImpl: evaluation failed");
		return std::nullopt;
	}

	FLASH_LOG(Templates, Debug, "substituteAndEvaluateNonTypeDefaultImpl: succeeded");
	if (template_args.size() < template_params.size()) {
		const TemplateParameterNode& param = template_params[template_args.size()];
		if (std::optional<TypeSpecifierNode> target_type =
				substituteNonTypeParameterTypeImpl(
					parser,
					param,
					template_params,
					template_args);
			target_type.has_value()) {
			return templateTypeArgFromEvalResult(eval_result, *target_type);
		}
	}
	return templateTypeArgFromEvalResult(eval_result);
}

	bool templateArgsStillNeedAliasLookupMaterialization(std::span<const TemplateTypeArg> template_args) {
		for (const TemplateTypeArg& arg : template_args) {
			if (arg.is_dependent ||
				arg.dependent_name.isValid() ||
				arg.dependent_expr.has_value()) {
				return true;
			}
			if (!arg.is_value &&
				arg.type_index.is_valid() &&
				typeIndexContainsDependentPlaceholder(arg.type_index)) {
				return true;
			}
		}
		return false;
	}

	template <typename MaterializeArgsFn, typename MaterializeLookupFn>
	TemplateTypeArg recursivelyMaterializeAliasLookupTemplateArg(
		const TemplateEnvironment& substitution_environment,
		TemplateTypeArg concrete_arg,
		MaterializeArgsFn&& materialize_args,
		MaterializeLookupFn&& materialize_lookup,
		int depth) {
		constexpr int kMaxRecursiveTemplateArgDepth = 8;
		if (depth >= kMaxRecursiveTemplateArgDepth ||
			concrete_arg.is_value ||
			!concrete_arg.type_index.is_valid()) {
			return concrete_arg;
		}

		auto tryRebindByName = [&](StringHandle dependent_name) -> bool {
			if (!dependent_name.isValid()) {
				return false;
			}
			if (std::optional<TemplateTypeArg> rebound =
					resolveContextBinding(
						dependent_name,
						substitution_environment);
				rebound.has_value()) {
				concrete_arg = !rebound->is_value
					? rebindDependentTemplateTypeArg(*rebound, concrete_arg)
					: *rebound;
				return true;
			}
			return false;
		};

		if (concrete_arg.is_dependent &&
			concrete_arg.dependent_name.isValid()) {
			(void)tryRebindByName(concrete_arg.dependent_name);
			if (concrete_arg.is_value || !concrete_arg.type_index.is_valid()) {
				return concrete_arg;
			}
		}

		const TypeInfo* concrete_type_info = tryGetTypeInfo(concrete_arg.type_index);
		if (concrete_type_info == nullptr) {
			return concrete_arg;
		}

		if (concrete_type_info->name().isValid() &&
			tryRebindByName(concrete_type_info->name())) {
			if (concrete_arg.is_value || !concrete_arg.type_index.is_valid()) {
				return concrete_arg;
			}
			concrete_type_info = tryGetTypeInfo(concrete_arg.type_index);
			if (concrete_type_info == nullptr) {
				return concrete_arg;
			}
		}

		ResolvedAliasTypeInfo resolved_arg_alias = resolveAliasTypeInfo(
			concrete_arg.type_index.withCategory(concrete_type_info->typeEnum()));
		if (resolved_arg_alias.terminal_type_info != nullptr &&
			resolved_arg_alias.terminal_type_info->typeEnum() != TypeCategory::Invalid) {
			concrete_type_info = resolved_arg_alias.terminal_type_info;
			TemplateTypeArg resolved_alias_arg = makeTemplateTypeArgFromResolvedAlias(
				resolved_arg_alias,
				concrete_type_info->registeredTypeIndex().withCategory(
					concrete_type_info->typeEnum()));
			concrete_arg.type_index = resolved_alias_arg.type_index;
			concrete_arg.setCategory(resolved_alias_arg.typeEnum());
			concrete_arg.pointer_depth = resolved_alias_arg.pointer_depth;
			concrete_arg.ref_qualifier = resolved_alias_arg.ref_qualifier;
			concrete_arg.cv_qualifier = resolved_alias_arg.cv_qualifier;
			concrete_arg.is_array = resolved_alias_arg.is_array;
			concrete_arg.array_dimensions = std::move(resolved_alias_arg.array_dimensions);
			concrete_arg.function_signature = resolved_alias_arg.function_signature;
			if (resolved_arg_alias.member_class_name.has_value()) {
				concrete_arg.member_class_name = *resolved_arg_alias.member_class_name;
			}
			concrete_arg.is_dependent = false;
			concrete_arg.dependent_name = {};
			concrete_arg.dependent_expr = std::nullopt;
		}

		if (!concrete_type_info->isTemplateInstantiation()) {
			return concrete_arg;
		}

		std::vector<TemplateTypeArg> nested_concrete_args =
			materialize_args(*concrete_type_info);
		for (TemplateTypeArg& nested_arg : nested_concrete_args) {
			nested_arg = recursivelyMaterializeAliasLookupTemplateArg(
				substitution_environment,
				std::move(nested_arg),
				materialize_args,
				materialize_lookup,
				depth + 1);
		}
		if (templateArgsStillNeedAliasLookupMaterialization(nested_concrete_args)) {
			return concrete_arg;
		}

		auto materialized_nested =
			materialize_lookup(*concrete_type_info, nested_concrete_args);
		const TypeInfo* resolved_nested_info = materialized_nested.resolved_type_info;
		if (resolved_nested_info == nullptr) {
			StringHandle canonical_name_handle =
				materialized_nested.canonicalNameHandle();
			if (canonical_name_handle.isValid()) {
				resolved_nested_info = findTypeByName(canonical_name_handle);
			}
		}
		if (resolved_nested_info == nullptr) {
			return concrete_arg;
		}

		TypeIndex resolved_nested_index =
			resolved_nested_info->registeredTypeIndex().withCategory(
				resolved_nested_info->typeEnum());
		TemplateTypeArg resolved_nested_arg =
			makeTemplateTypeArgFromResolvedAlias(
				resolveAliasTypeInfo(resolved_nested_index),
				resolved_nested_index);
		resolved_nested_arg.is_pack = concrete_arg.is_pack;
		return rebindDependentTemplateTypeArg(
			resolved_nested_arg,
			concrete_arg);
	}

}  // namespace

ASTNode Parser::substituteNonTypeDefaultExpression(
	const ASTNode& default_node,
	const InlineVector<TemplateParameterNode, 4>& template_params,
	std::span<const TemplateTypeArg> template_args) {
	return substituteNonTypeDefaultExpressionImpl(
		*this,
		default_node,
		template_params,
		template_args);
}

std::optional<TemplateTypeArg> Parser::substituteAndEvaluateNonTypeDefault(
	const ASTNode& default_node,
	const InlineVector<TemplateParameterNode, 4>& template_params,
	std::span<const TemplateTypeArg> template_args) {
	InlineVector<std::string_view, 4> derived_param_names;
	derived_param_names.reserve(template_params.size());
	for (const TemplateParameterNode& template_param : template_params) {
		derived_param_names.push_back(template_param.name());
	}
	return substituteAndEvaluateNonTypeDefaultImpl(
		*this,
		default_node,
		template_params,
		template_args,
		std::span<const std::string_view>(derived_param_names.data(), derived_param_names.size()));
}

std::optional<TemplateTypeArg> Parser::substituteAndEvaluateNonTypeDefault(
	const ASTNode& default_node,
	const InlineVector<TemplateParameterNode, 4>& template_params,
	std::span<const TemplateTypeArg> template_args,
	std::span<const std::string_view> template_param_names) {
	return substituteAndEvaluateNonTypeDefaultImpl(
		*this,
		default_node,
		template_params,
		template_args,
		template_param_names);
}

std::string_view Parser::get_instantiated_class_name(std::string_view template_name, std::span<const TemplateTypeArg> template_args) {
	if (size_t last_colon = template_name.rfind("::"); last_colon != std::string_view::npos) {
		template_name = template_name.substr(last_colon + 2);
	}
	auto result = FlashCpp::generateInstantiatedNameFromArgs(template_name, template_args);
	return result;
}

std::optional<TemplateTypeArg> Parser::materializeDeferredAliasTemplateArg(
	const ASTNode& arg_node,
	const InlineVector<TemplateParameterNode, 4>& template_parameters,
	const InlineVector<StringHandle, 4>& param_names,
	std::span<const TemplateTypeArg> template_args,
	const TemplateParameterNode* target_template_param) {
#if WITH_PARSER_RUNTIME_STATS
	FLASHCPP_PARSER_RUNTIME_PHASE(AliasMaterialization);
#endif
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
		if (alias_param_idx < template_parameters.size()) {
			const TemplateParameterNode& alias_param = template_parameters[alias_param_idx];
			if (alias_param.kind() == TemplateParameterKind::NonType && !normalized.is_value) {
				normalized.is_value = true;
				normalized.is_dependent = normalized.is_dependent || normalized.dependent_name.isValid();
				if (alias_param.has_type()) {
					const auto& param_type = alias_param.type_specifier_node();
					normalized.type_index = param_type.type_index();
					normalized.setCategory(param_type.type());
				} else if (!normalized.type_index.is_valid()) {
					throw InternalError("Non-type alias template parameter is missing a declared type");
				}
			}
		}
		return normalized;
	};
	const auto make_dependent_value_for_alias_param = [&](StringHandle param_name) -> std::optional<TemplateTypeArg> {
		auto alias_param_idx = find_param_index(param_name);
		if (!alias_param_idx.has_value() || *alias_param_idx >= template_parameters.size()) {
			return std::nullopt;
		}
		const TemplateParameterNode& alias_param = template_parameters[*alias_param_idx];
		if (alias_param.kind() != TemplateParameterKind::NonType) {
			return std::nullopt;
		}
		if (!alias_param.has_type()) {
			throw InternalError("Non-type alias template parameter is missing a declared type");
		}
		return TemplateTypeArg::makeDependentValue(
			param_name,
			alias_param.type_specifier_node().type());
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
		if (arg_type.type_index().is_valid()) {
			if (const TypeInfo* arg_type_info = tryGetTypeInfo(arg_type.type_index());
				arg_type_info != nullptr &&
				arg_type_info->isTemplateInstantiation()) {
				TemplateEnvironment substitution_environment = buildTemplateEnvironment(
					std::span<const TemplateParameterNode>(
						template_parameters.data(),
						template_parameters.size()),
					template_args,
					nullptr);
				auto eval_nttp = [this](
					const ASTNode& expr,
					std::span<const ASTNode> params,
					std::span<const TemplateTypeArg> args) -> std::optional<TemplateTypeArg> {
					return this->evaluateDependentNTTPExpression(expr, params, args);
				};
				auto materialize_args = [&](const TypeInfo& source_type_info) {
					return materializeTemplateArgs(
						source_type_info,
						template_parameters,
						template_args,
						eval_nttp);
				};
				auto materialize_lookup =
					[this](const TypeInfo& source_type_info, std::span<const TemplateTypeArg> concrete_instantiation_args) {
						StringHandle qualified_base_template_name =
							gNamespaceRegistry.buildQualifiedIdentifier(
								source_type_info.sourceNamespace(),
								source_type_info.baseTemplateName());
						std::string_view base_template_name =
							StringTable::getStringView(qualified_base_template_name);
						AliasTemplateMaterializationResult materialized_type;
						if (!base_template_name.empty()) {
							materialized_type =
								materializeTemplateInstantiationForLookup(
									base_template_name,
									concrete_instantiation_args);
						}
						if ((materialized_type.resolved_type_info == nullptr &&
							 materialized_type.instantiated_name.empty()) &&
							qualified_base_template_name != source_type_info.baseTemplateName()) {
							materialized_type =
								materializeTemplateInstantiationForLookup(
									StringTable::getStringView(source_type_info.baseTemplateName()),
									concrete_instantiation_args);
						}
						return materialized_type;
					};
				std::vector<TemplateTypeArg> concrete_instantiation_args =
					materialize_args(*arg_type_info);
				for (TemplateTypeArg& concrete_arg : concrete_instantiation_args) {
					concrete_arg = recursivelyMaterializeAliasLookupTemplateArg(
						substitution_environment,
						std::move(concrete_arg),
						materialize_args,
						materialize_lookup,
						0);
				}
				if (!StringTable::getStringView(arg_type_info->baseTemplateName()).empty()) {
					AliasTemplateMaterializationResult materialized_type =
						materialize_lookup(*arg_type_info, concrete_instantiation_args);
					const TypeInfo* resolved_type_info = materialized_type.resolved_type_info;
					if (resolved_type_info == nullptr &&
						!materialized_type.instantiated_name.empty()) {
						resolved_type_info = findTypeByName(
							StringTable::getOrInternStringHandle(materialized_type.instantiated_name));
					}
					if (resolved_type_info != nullptr) {
						return resolveTypeInfoToTemplateArg(*resolved_type_info, arg_type);
					}
				}
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
		return make_dependent_value_for_alias_param(tparam_ref->param_name());
	}

	if (const auto* id = std::get_if<IdentifierNode>(&arg_expr)) {
		StringHandle id_handle = StringTable::getOrInternStringHandle(id->name());
		if (auto alias_param_idx = find_param_index(id_handle);
			alias_param_idx.has_value() && *alias_param_idx < template_args.size()) {
			return normalize_alias_param_arg(*alias_param_idx, template_args[*alias_param_idx]);
		}

		return make_dependent_value_for_alias_param(id_handle);
	}

	// TypeTraitExprNode arguments (e.g. __is_final(T), __is_empty(T)) must NOT be
	// evaluated before template parameter substitution: the placeholder type held
	// inside them does not carry flags like is_final/is_empty, so an early
	// unsubstituted evaluation would silently return the wrong value (false) and
	// prevent the correct substituted evaluation below from running.  The same
	// rule applies to all dependent NTTP expressions: bind alias parameters first,
	// then perform exactly one substitute/evaluate pass below.  Do not add a
	// context-free evaluation fallback here, because it can bind hidden outer names
	// before alias-parameter substitution and produce a different value.
	const bool is_type_trait_expr = std::get_if<TypeTraitExprNode>(&arg_expr) != nullptr;
	if (is_type_trait_expr) {
		FLASH_LOG(Templates, Debug, "materializeDeferredAliasTemplateArg: skipping early eval for TypeTraitExprNode");

		// If the type arguments that feed this TypeTraitExpr are still dependent
		// (e.g. __is_final(H) where H is an outer template parameter of HeadBase),
		// evaluating the trait on the placeholder type would silently return false
		// and poison the cached instantiation. Instead, return a dependent bool
		// placeholder so the downstream code registers a dependent Cond<dep,...>
		// rather than a concrete (wrong) one. When the outer template is later
		// instantiated with a concrete type (e.g. FinalHead), this function will
		// be called again with non-dependent args and will evaluate correctly.
		const auto unresolved_dependent_anchor = [](const TemplateTypeArg& candidate) -> StringHandle {
			if (candidate.dependent_name.isValid()) {
				return candidate.dependent_name;
			}
			if (!candidate.is_value &&
				candidate.type_index.is_valid() &&
				typeIndexContainsDependentPlaceholder(candidate.type_index)) {
				if (const TypeInfo* type_info = tryGetTypeInfo(candidate.type_index);
					type_info != nullptr &&
					type_info->name().isValid()) {
					return type_info->name();
				}
			}
			return {};
		};
		for (const TemplateTypeArg& arg : template_args) {
			if (StringHandle dependent_anchor = unresolved_dependent_anchor(arg);
				dependent_anchor.isValid()) {
				FLASH_LOG(Templates, Debug, "materializeDeferredAliasTemplateArg: arg is dependent, returning dependent bool placeholder");
				// Pre-substitute the alias template parameters (e.g. 'Type' in __is_final(Type))
				// into the outer dependent parameter (e.g. 'Head') so that when the outer
				// template is later instantiated, materializeStoredTemplateArgs can find
				// the outer parameter's name in param_map_ and evaluate correctly.
				ASTNode pre_substituted = substituteNonTypeDefaultExpressionImpl(
					*this,
					arg_node,
					template_parameters,
					std::span<const TemplateTypeArg>(template_args.data(), template_args.size()));
				return TemplateTypeArg::makeDependentValue(dependent_anchor, TypeCategory::Bool, 0, pre_substituted);
			}
		}
	}

	// Handle sizeof...(Pack) directly and preserve the target NTTP category.
	// The generic substitute/evaluate path now resolves pack size through the
	// template evaluation environment. This fast path is still needed when alias
	// target substitution must preserve the destination NTTP category immediately
	// (e.g., bool/enum/non-default integral targets) instead of accepting the
	// generic evaluator's default unsigned-long-long category.
	if (const auto* sizeof_pack = std::get_if<SizeofPackNode>(&arg_expr)) {
		std::string_view pack_name = sizeof_pack->pack_name();
		std::span<const TemplateParameterNode> params_span(
			template_parameters.data(), template_parameters.size());
		if (auto pack_size = countPackSizeFromParams(pack_name, params_span, template_args.size())) {
			FLASH_LOG(Templates, Debug, "materializeDeferredAliasTemplateArg: sizeof...(", pack_name, ") = ", *pack_size, " (counted from template_parameters/args)");
			TypeCategory value_cat = TypeCategory::UnsignedLongLong;
			if (target_template_param != nullptr && target_template_param->has_type()) {
				value_cat = target_template_param->type_specifier_node().type();
			}
			return TemplateTypeArg(static_cast<int64_t>(*pack_size), value_cat);
		}
	}

	InlineVector<TemplateParameterNode, 4> typed_template_parameters;
	typed_template_parameters.reserve(template_parameters.size());
	for (const TemplateParameterNode& template_param : template_parameters) {
		typed_template_parameters.push_back(template_param);
	}

	if (auto substituted_eval = substituteAndEvaluateNonTypeDefault(
			arg_node,
			typed_template_parameters,
			std::span<const TemplateTypeArg>(template_args.data(), template_args.size()))) {
		return *substituted_eval;
	}

	if (const auto* qual_id = std::get_if<QualifiedIdentifierNode>(&arg_expr)) {
		return classifyDeferredQualifiedIdentifier(*qual_id, target_template_param);
	}

	return std::nullopt;
}

std::optional<TemplateTypeArg> Parser::materializeDeferredAliasTemplateArg(
	const ASTNode& arg_node,
	const InlineVector<ASTNode, 4>& template_parameters,
	const InlineVector<StringHandle, 4>& param_names,
	std::span<const TemplateTypeArg> template_args,
	const TemplateParameterNode* target_template_param) {
	InlineVector<TemplateParameterNode, 4> typed_template_parameters;
	typed_template_parameters.reserve(template_parameters.size());
	for (const ASTNode& template_param : template_parameters) {
		const TemplateParameterNode* typed_param = tryGetTemplateParameterNode(template_param);
		if (typed_param == nullptr) {
			return std::nullopt;
		}
		typed_template_parameters.push_back(*typed_param);
	}
	return materializeDeferredAliasTemplateArg(
		arg_node,
		typed_template_parameters,
		param_names,
		template_args,
		target_template_param);
}

std::optional<InlineVector<TemplateTypeArg, 4>> Parser::materializeDeferredAliasTemplateArgs(
	const TemplateAliasNode& alias_node,
	std::span<const TemplateTypeArg> template_args) {
	InlineVector<TemplateTypeArg, 4> substituted_args;
	const auto& param_names = alias_node.template_param_names();
	std::span<const ASTNode> target_template_args = alias_node.target_template_args();
	std::span<const TemplateParameterNode> alias_params_span(
		alias_node.template_parameters().data(),
		alias_node.template_parameters().size());
	const auto target_template_params =
		getTargetTemplateParameters(StringTable::getOrInternStringHandle(alias_node.target_template_name()));
	const auto getForwardedPackRange =
		[&](const ASTNode& target_arg_node) -> std::optional<std::pair<size_t, size_t>> {
		if (std::optional<std::string_view> pack_name =
				tryGetAliasPackForwardingName(target_arg_node);
			pack_name.has_value()) {
			return findPackArgRangeFromParams(
				*pack_name,
				alias_params_span,
				template_args.size());
		}
		return std::nullopt;
	};
	size_t estimated_arg_count = target_template_args.size();
	for (const ASTNode& target_arg_node : target_template_args) {
		if (auto pack_range = getForwardedPackRange(target_arg_node);
			pack_range.has_value() && pack_range->second > 1) {
			estimated_arg_count += pack_range->second - 1;
		}
	}
	substituted_args.reserve(estimated_arg_count);

	auto getTargetTemplateParam = [&](size_t index) -> const TemplateParameterNode* {
		if (index < target_template_params.size()) {
			return &target_template_params[index];
		}
		if (!target_template_params.empty() && target_template_params.back().is_variadic()) {
			return &target_template_params.back();
		}
		return nullptr;
	};

	for (size_t i = 0; i < target_template_args.size(); ++i) {
		const TemplateParameterNode* target_template_param = getTargetTemplateParam(i);
		const ASTNode& target_arg_node = target_template_args[i];
		if (auto pack_range = getForwardedPackRange(target_arg_node);
			pack_range.has_value()) {
			for (size_t offset = 0; offset < pack_range->second; ++offset) {
				const size_t arg_index = pack_range->first + offset;
				if (arg_index >= template_args.size()) {
					break;
				}
				substituted_args.push_back(template_args[arg_index]);
			}
			continue;
		}
		auto materialized_arg = materializeDeferredAliasTemplateArg(
			target_arg_node,
			alias_node.template_parameters(),
			param_names,
			template_args,
			target_template_param);
		if (!materialized_arg.has_value()) {
			return std::nullopt;
		}
		substituted_args.push_back(std::move(*materialized_arg));
	}

	return substituted_args;
}

StringHandle Parser::getDeferredMemberAliasHandle(
	const TemplateAliasNode& alias_node,
	std::string_view instantiated_name) const {
	StringHandle member_alias_handle =
		StringTable::getOrInternStringHandle(
			StringBuilder()
				.append(instantiated_name)
				.append("::")
				.append(alias_node.targetMemberTemplateName())
				.commit());
	if (gTemplateRegistry.lookup_alias_template(member_alias_handle).has_value()) {
		return member_alias_handle;
	}
	return alias_node.targetMemberTemplateNameHandle();
}

std::optional<InlineVector<TemplateTypeArg, 4>> Parser::materializeDeferredAliasMemberTemplateArgs(
	const TemplateAliasNode& alias_node,
	std::span<const TemplateTypeArg> template_args,
	StringHandle member_alias_handle) {
	const auto member_template_params = getTargetTemplateParameters(member_alias_handle);
	InlineVector<TemplateTypeArg, 4> member_args;
	std::span<const ASTNode> member_template_args = alias_node.targetMemberTemplateArgs();
	member_args.reserve(member_template_args.size());

	for (size_t i = 0; i < member_template_args.size(); ++i) {
		const TemplateParameterNode* target_template_param = nullptr;
		if (i < member_template_params.size()) {
			target_template_param = &member_template_params[i];
		} else if (!member_template_params.empty() && member_template_params.back().is_variadic()) {
			target_template_param = &member_template_params.back();
		}

		auto materialized_member_arg = materializeDeferredAliasTemplateArg(
			member_template_args[i],
			alias_node.template_parameters(),
			alias_node.template_param_names(),
			template_args,
			target_template_param);
		if (!materialized_member_arg.has_value()) {
			return std::nullopt;
		}
		member_args.push_back(std::move(*materialized_member_arg));
	}

	return member_args;
}

StringHandle Parser::getAliasTargetNameHandle(const TypeSpecifierNode& alias_target) const {
	if (const TypeInfo* alias_target_info = tryGetTypeInfo(alias_target.type_index())) {
		return alias_target_info->name();
	}
	if (alias_target.token().handle().isValid()) {
		return alias_target.token().handle();
	}
	return {};
}

std::optional<size_t> Parser::findAliasTargetTemplateParamIndex(
	const TemplateAliasNode& alias_node,
	std::span<const TemplateTypeArg> concrete_args) const {
	StringHandle alias_target_name = getAliasTargetNameHandle(alias_node.target_type_node());
	if (!alias_target_name.isValid()) {
		return std::nullopt;
	}

	const auto& alias_param_names = alias_node.template_param_names();
	size_t max_alias_param_index = std::min(alias_param_names.size(), concrete_args.size());
	for (size_t alias_param_index = 0; alias_param_index < max_alias_param_index; ++alias_param_index) {
		if (alias_param_names[alias_param_index] == alias_target_name) {
			return alias_param_index;
		}
	}
	return std::nullopt;
}

std::optional<TemplateTypeArg> Parser::tryRebindAliasTargetTemplateArg(
	const TemplateAliasNode& alias_node,
	std::span<const TemplateTypeArg> concrete_args) const {
	std::optional<size_t> alias_param_index =
		findAliasTargetTemplateParamIndex(alias_node, concrete_args);
	if (!alias_param_index.has_value()) {
		return std::nullopt;
	}
	return rebindDependentTemplateTypeArg(
		concrete_args[*alias_param_index],
		TemplateTypeArg(alias_node.target_type_node()));
}


void Parser::normalizeDependentNonTypeTemplateArgs(
	std::span<const TemplateParameterNode> template_parameters,
	std::vector<TemplateTypeArg>& template_args) {
	size_t arg_index = 0;
	for (size_t param_index = 0;
		 param_index < template_parameters.size() && arg_index < template_args.size();
		 ++param_index) {
		const TemplateParameterNode& template_param = template_parameters[param_index];
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
			if (template_param.has_type()) {
				value_category = template_param.type_specifier_node().category();
			}
			TypeIndex value_type_index = nativeTypeIndex(value_category);
			arg.type_index = value_type_index.is_valid()
				? value_type_index
				: TypeIndex{0, value_category};
		}

		++arg_index;
	}
}

void Parser::normalizeDependentNonTypeTemplateArgs(
	std::span<const TemplateParameterNode> template_parameters,
	InlineVector<TemplateTypeArg, 4>& template_args) {
	size_t arg_index = 0;
	for (size_t param_index = 0;
		 param_index < template_parameters.size() && arg_index < template_args.size();
		 ++param_index) {
		const TemplateParameterNode& template_param = template_parameters[param_index];
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
			if (template_param.has_type()) {
				value_category = template_param.type_specifier_node().category();
			}
			TypeIndex value_type_index = nativeTypeIndex(value_category);
			arg.type_index = value_type_index.is_valid()
				? value_type_index
				: TypeIndex{0, value_category};
		}

		++arg_index;
	}
}

void Parser::normalizeDependentNonTypeTemplateArgs(
	const InlineVector<ASTNode, 4>& template_parameters,
	std::vector<TemplateTypeArg>& template_args) {
	InlineVector<TemplateParameterNode, 4> typed_template_parameters =
		collectTemplateParameterNodes(
			std::span<const ASTNode>(template_parameters.data(), template_parameters.size()));
	normalizeDependentNonTypeTemplateArgs(
		typed_template_parameters,
		template_args);
}

void Parser::normalizeDependentNonTypeTemplateArgs(
	const InlineVector<ASTNode, 4>& template_parameters,
	InlineVector<TemplateTypeArg, 4>& template_args) {
	InlineVector<TemplateParameterNode, 4> typed_template_parameters =
		collectTemplateParameterNodes(
			std::span<const ASTNode>(template_parameters.data(), template_parameters.size()));
	normalizeDependentNonTypeTemplateArgs(
		typed_template_parameters,
		template_args);
}

Parser::AliasTemplateMaterializationResult Parser::materializeAliasTemplateInstantiation(
	std::string_view alias_template_name,
	std::span<const TemplateTypeArg> template_args) {
	AliasTemplateMaterializationResult result;
	const TemplateAliasNode* alias_node = nullptr;
	if (auto alias_entry = gTemplateRegistry.lookup_alias_template(alias_template_name);
		alias_entry.has_value() && alias_entry->is<TemplateAliasNode>()) {
		alias_node = &alias_entry->as<TemplateAliasNode>();
	}
	auto alias_preserves_surface_type = [](const ResolvedAliasTypeInfo& resolved_alias) {
		return resolved_alias.cv_qualifier != CVQualifier::None ||
			   resolved_alias.pointer_depth != 0 ||
			   resolved_alias.reference_qualifier != ReferenceQualifier::None ||
			   resolved_alias.function_signature.has_value() ||
			   resolved_alias.member_class_name.has_value() ||
			   !resolved_alias.array_dimensions.empty();
	};
	auto tryResolveDirectAliasTarget = [&]() -> bool {
		if (alias_node == nullptr) {
			return false;
		}
		std::optional<TemplateTypeArg> rebound_arg =
			tryRebindAliasTargetTemplateArg(*alias_node, template_args);
		if (!rebound_arg.has_value() || rebound_arg->is_value) {
			return false;
		}
		const TypeInfo* rebound_type_info = tryGetTypeInfo(rebound_arg->type_index);
		if (rebound_type_info == nullptr) {
			std::string_view builtin_type_name = getTypeName(rebound_arg->category());
			if (builtin_type_name.empty()) {
				return false;
			}
			TypeIndex native_type_index = nativeTypeIndex(rebound_arg->category());
			if (native_type_index.is_valid()) {
				result.resolved_type_info = tryGetTypeInfo(
					native_type_index.withCategory(rebound_arg->category()));
			}
			if (result.resolved_type_info == nullptr) {
				result.resolved_type_info = findTypeByName(
					StringTable::getOrInternStringHandle(builtin_type_name));
			}
			result.instantiated_name = builtin_type_name;
			return true;
		}
		ResolvedAliasTypeInfo resolved_rebound_alias = resolveAliasTypeInfo(
			rebound_type_info->registeredTypeIndex().withCategory(
				rebound_type_info->typeEnum()));
		if (resolved_rebound_alias.terminal_type_info != nullptr &&
			!alias_preserves_surface_type(resolved_rebound_alias)) {
			rebound_type_info = resolved_rebound_alias.terminal_type_info;
		}
		result.resolved_type_info = rebound_type_info;
		result.instantiated_name = StringTable::getStringView(rebound_type_info->name());
		return true;
	};
	auto materializeTemplateArgsForLookup =
		[&](const TypeInfo& source_type_info) -> std::vector<TemplateTypeArg> {
		// Evaluator for dependent NTTP expressions (e.g. __is_final(Head) -> false).
		// Uses the Parser's active substitution context so outer bindings like Head->Empty
		// are visible even when the stored dependent_expr references the original param name.
		auto eval_nttp = [this](
			const ASTNode& expr,
			std::span<const ASTNode> params,
			std::span<const TemplateTypeArg> args) -> std::optional<TemplateTypeArg> {
			return this->evaluateDependentNTTPExpression(expr, params, args);
		};
		auto materialize_args = [&](const TypeInfo& nested_source_type_info) {
			return materializeTemplateArgs(
				nested_source_type_info,
				alias_node->template_parameters(),
				template_args,
				eval_nttp);
		};
		std::vector<TemplateTypeArg> concrete_args =
			materialize_args(source_type_info);
		TemplateEnvironment substitution_environment = buildTemplateEnvironment(
			std::span<const TemplateParameterNode>(
				alias_node->template_parameters().data(),
				alias_node->template_parameters().size()),
			template_args,
			nullptr);
		auto materialize_lookup =
			[this](const TypeInfo& nested_type_info, std::span<const TemplateTypeArg> nested_concrete_args) {
				StringHandle qualified_base_template_name =
					gNamespaceRegistry.buildQualifiedIdentifier(
						nested_type_info.sourceNamespace(),
						nested_type_info.baseTemplateName());
				std::string_view base_template_name =
					StringTable::getStringView(qualified_base_template_name);
				AliasTemplateMaterializationResult materialized_nested;
				if (!base_template_name.empty()) {
					materialized_nested =
						materializeTemplateInstantiationForLookup(
							base_template_name,
							nested_concrete_args);
				}
				if ((materialized_nested.resolved_type_info == nullptr &&
					 materialized_nested.instantiated_name.empty()) &&
					qualified_base_template_name != nested_type_info.baseTemplateName()) {
					materialized_nested =
						materializeTemplateInstantiationForLookup(
							StringTable::getStringView(nested_type_info.baseTemplateName()),
							nested_concrete_args);
				}
				return materialized_nested;
			};
		for (TemplateTypeArg& concrete_arg : concrete_args) {
			concrete_arg = recursivelyMaterializeAliasLookupTemplateArg(
				substitution_environment,
				std::move(concrete_arg),
				materialize_args,
				materialize_lookup,
				0);
		}
		return concrete_args;
	};
	auto tryMaterializeTemplateAliasTarget = [&]() -> bool {
		if (alias_node == nullptr) {
			return false;
		}

		const TypeSpecifierNode& target_type_spec = alias_node->target_type_node();
		const TypeInfo* target_type_info = tryGetTypeInfo(target_type_spec.type_index());
		if (target_type_info == nullptr || !target_type_info->isTemplateInstantiation()) {
			return false;
		}

		std::vector<TemplateTypeArg> concrete_target_args =
			materializeTemplateArgsForLookup(*target_type_info);
		StringHandle qualified_target_template_name =
			gNamespaceRegistry.buildQualifiedIdentifier(
				target_type_info->sourceNamespace(),
				target_type_info->baseTemplateName());
		std::string_view target_template_name =
			StringTable::getStringView(qualified_target_template_name);
		if (target_template_name.empty()) {
			target_template_name =
				StringTable::getStringView(target_type_info->baseTemplateName());
		}
		if (target_template_name.empty()) {
			return false;
		}

		AliasTemplateMaterializationResult materialized_target =
			materializeTemplateInstantiationForLookup(
				target_template_name,
				concrete_target_args);
		if (materialized_target.resolved_type_info == nullptr &&
			qualified_target_template_name != target_type_info->baseTemplateName()) {
			materialized_target = materializeTemplateInstantiationForLookup(
				StringTable::getStringView(target_type_info->baseTemplateName()),
				concrete_target_args);
		}
		if (materialized_target.resolved_type_info == nullptr &&
			materialized_target.instantiated_name.empty()) {
			const size_t alias_owner_sep = alias_template_name.rfind("::");
			const std::string_view target_base_name =
				StringTable::getStringView(target_type_info->baseTemplateName());
			if (alias_owner_sep != std::string_view::npos &&
				target_base_name.find("::") == std::string_view::npos) {
				const std::string_view owner_member_template_name =
					StringBuilder()
						.append(alias_template_name.substr(0, alias_owner_sep))
						.append("::")
						.append(target_base_name)
						.commit();
				materialized_target = materializeTemplateInstantiationForLookup(
					owner_member_template_name,
					concrete_target_args);
			}
		}
		if (materialized_target.resolved_type_info == nullptr &&
			materialized_target.instantiated_name.empty()) {
			return false;
		}

		result.instantiated_name = materialized_target.instantiated_name;
		result.resolved_type_info = materialized_target.resolved_type_info;
		if (result.resolved_type_info != nullptr && result.instantiated_name.empty()) {
			result.instantiated_name = StringTable::getStringView(result.resolved_type_info->name());
		}
		return true;
	};
	std::string_view resolved_name = alias_template_name;
	result.instantiated_name = instantiate_and_register_base_template(resolved_name, template_args);
	if (result.instantiated_name.empty()) {
		// Direct parameter aliases such as `template<class T> using id = T`
		// do not produce an instantiated helper type name; resolve them by
		// rebinding the alias target parameter to the caller's concrete argument.
		if (!tryResolveDirectAliasTarget()) {
			(void)tryMaterializeTemplateAliasTarget();
		}
		return result;
	}

	result.resolved_type_info =
		findTypeByName(StringTable::getOrInternStringHandle(result.instantiated_name));
	if (alias_node != nullptr &&
		!alias_node->is_deferred()) {
		const TypeSpecifierNode& alias_target_type = alias_node->target_type_node();
		if (const TypeInfo* alias_target_info = tryGetTypeInfo(alias_target_type.type_index());
			alias_target_info != nullptr && alias_target_info->isTemplateInstantiation()) {
			std::vector<TemplateTypeArg> concrete_target_args =
				materializeTemplateArgsForLookup(*alias_target_info);
			StringHandle qualified_target_template_name =
				gNamespaceRegistry.buildQualifiedIdentifier(
					alias_target_info->sourceNamespace(),
					alias_target_info->baseTemplateName());
			std::string_view target_template_name =
				StringTable::getStringView(qualified_target_template_name);
			if (target_template_name.empty()) {
				target_template_name =
					StringTable::getStringView(alias_target_info->baseTemplateName());
			}
			if (!target_template_name.empty()) {
				AliasTemplateMaterializationResult materialized_target =
					materializeTemplateInstantiationForLookup(
						target_template_name,
						concrete_target_args);
				if (materialized_target.resolved_type_info == nullptr &&
					qualified_target_template_name != alias_target_info->baseTemplateName()) {
					materialized_target = materializeTemplateInstantiationForLookup(
						StringTable::getStringView(alias_target_info->baseTemplateName()),
						concrete_target_args);
				}
				if (materialized_target.resolved_type_info == nullptr &&
					materialized_target.instantiated_name.empty()) {
					const size_t alias_owner_sep = alias_template_name.rfind("::");
					const std::string_view target_base_name =
						StringTable::getStringView(alias_target_info->baseTemplateName());
					if (alias_owner_sep != std::string_view::npos &&
						target_base_name.find("::") == std::string_view::npos) {
						const std::string_view owner_member_template_name =
							StringBuilder()
								.append(alias_template_name.substr(0, alias_owner_sep))
								.append("::")
								.append(target_base_name)
								.commit();
						materialized_target = materializeTemplateInstantiationForLookup(
							owner_member_template_name,
							concrete_target_args);
					}
				}
				if (!materialized_target.instantiated_name.empty() ||
					materialized_target.resolved_type_info != nullptr) {
					result = std::move(materialized_target);
				}
			}
		}
	}
	const size_t alias_template_member_sep = alias_template_name.rfind("::");
	const bool is_qualified_alias_template =
		alias_template_member_sep != std::string_view::npos;
	if (alias_node != nullptr &&
		alias_node->is_deferred() &&
		is_qualified_alias_template &&
		// Qualified member aliases can target another alias template (e.g.
		// `Checker::cond_t<T> = ::enable_if_t<...>`). Same-name aliases are
		// deliberately left on the ordinary member-target path to avoid
		// recursive self-materialization.
		alias_node->target_template_name() != alias_template_name &&
		gTemplateRegistry.lookup_alias_template(alias_node->target_template_name()).has_value()) {
		if (auto substituted_args_opt =
				materializeDeferredAliasTemplateArgs(*alias_node, template_args);
			substituted_args_opt.has_value()) {
			AliasTemplateMaterializationResult materialized_target_alias =
				materializeAliasTemplateInstantiation(
					alias_node->target_template_name(),
					*substituted_args_opt);
			if (materialized_target_alias.resolved_type_info != nullptr) {
				result.instantiated_name = materialized_target_alias.instantiated_name;
				result.resolved_type_info = materialized_target_alias.resolved_type_info;
			}
		}
	}
	if (alias_node != nullptr &&
		alias_node->hasDeferredMemberTarget() &&
		!alias_node->targetMemberTemplateArgs().empty() &&
		!result.instantiated_name.empty()) {
		StringHandle member_alias_handle =
			getDeferredMemberAliasHandle(*alias_node, result.instantiated_name);
		auto member_alias_entry = gTemplateRegistry.lookup_alias_template(member_alias_handle);
		if (member_alias_entry.has_value()) {
			if (auto concrete_member_args =
					materializeDeferredAliasMemberTemplateArgs(
						*alias_node,
						template_args,
						member_alias_handle)) {
				AliasTemplateMaterializationResult materialized_member_alias =
					materializeAliasTemplateInstantiation(
						StringTable::getStringView(member_alias_handle),
						*concrete_member_args);
				if (materialized_member_alias.resolved_type_info != nullptr) {
					result.instantiated_name = materialized_member_alias.instantiated_name;
					result.resolved_type_info = materialized_member_alias.resolved_type_info;
				}
			}
		}
	}
	if (alias_node != nullptr &&
		result.resolved_type_info != nullptr) {
		// Prefer a direct alias target over the helper instantiation itself.
		// Member-alias targets are checked after this because an alias target like
		// `Owner<B>::template type<T, F>` must first select `Owner<B>` and then
		// instantiate the selected member alias with its own `<T, F>` arguments.
		tryResolveDirectAliasTarget();
		if (const TypeInfo* concrete_member_alias =
				materializeInstantiatedMemberAliasTarget(
					alias_node->target_type_node(),
					alias_node->template_parameters(),
					template_args);
			concrete_member_alias != nullptr) {
			const TypeInfo* resolved_member_info = concrete_member_alias;
			ResolvedAliasTypeInfo resolved_member_alias = resolveAliasTypeInfo(
				concrete_member_alias->registeredTypeIndex().withCategory(
					concrete_member_alias->typeEnum()));
			if (resolved_member_alias.terminal_type_info != nullptr &&
				!alias_preserves_surface_type(resolved_member_alias)) {
				resolved_member_info = resolved_member_alias.terminal_type_info;
			}
			result.resolved_type_info = resolved_member_info;
			result.instantiated_name =
				StringTable::getStringView(resolved_member_info->name());
		}
	}
	if (alias_node != nullptr &&
		result.resolved_type_info != nullptr) {
		std::string_view unqualified_alias_instantiated_name =
			get_instantiated_class_name(alias_template_name, template_args);
		StringHandle alias_instantiated_handle = StringTable::getOrInternStringHandle(
			unqualified_alias_instantiated_name);
		if (is_qualified_alias_template) {
			alias_instantiated_handle = StringTable::getOrInternStringHandle(
				StringBuilder()
					.append(alias_template_name.substr(0, alias_template_member_sep + 2))
					.append(unqualified_alias_instantiated_name)
					.commit());
		}
		const TypeInfo& resolved_type_info = *result.resolved_type_info;
		TypeIndex alias_target_index =
			resolved_type_info.registeredTypeIndex().withCategory(
				resolved_type_info.typeEnum());
		TypeSpecifierNode alias_registration_type_spec = alias_node->target_type_node();
		if (const TypeSpecifierNode* resolved_alias_spec =
				resolved_type_info.aliasTypeSpecifier()) {
			alias_registration_type_spec = *resolved_alias_spec;
		} else {
			alias_registration_type_spec.set_type_index(alias_target_index);
			alias_registration_type_spec.set_category(resolved_type_info.typeEnum());
			alias_registration_type_spec.set_size_in_bits(resolved_type_info.sizeInBits());
		}

		auto registerOrUpdateAlias = [&](StringHandle alias_handle) {
			TypeInfo* alias_type_info = nullptr;
			auto existing_alias_it = getTypesByNameMap().find(alias_handle);
			if (existing_alias_it != getTypesByNameMap().end() &&
				existing_alias_it->second != nullptr) {
				alias_type_info = existing_alias_it->second;
				update_type_alias_copy(
					*alias_type_info,
					alias_target_index,
					resolved_type_info.sizeInBits().value,
					&alias_registration_type_spec,
					&resolved_type_info);
			}
			// Only normalize entries that were already created by earlier
			// parsing/materialization. Creating fresh aliases here changes
			// ordinary alias-chain lookup and can perturb sizeof/NTTP behavior.
			return alias_type_info;
		};

		registerOrUpdateAlias(alias_instantiated_handle);
		if (alias_instantiated_handle.view() != unqualified_alias_instantiated_name) {
			registerOrUpdateAlias(
				StringTable::getOrInternStringHandle(unqualified_alias_instantiated_name));
		}
	}
	return result;
}

Parser::AliasTemplateMaterializationResult Parser::materializeTemplateInstantiationForLookup(
	std::string_view template_name,
	std::span<const TemplateTypeArg> template_args) {
	auto resolve_builtin_type_info_by_name = [](std::string_view builtin_name) -> const TypeInfo* {
		constexpr TypeCategory builtin_categories[] = {
			TypeCategory::Void,
			TypeCategory::Nullptr,
			TypeCategory::Bool,
			TypeCategory::Char,
			TypeCategory::UnsignedChar,
			TypeCategory::Short,
			TypeCategory::UnsignedShort,
			TypeCategory::Int,
			TypeCategory::UnsignedInt,
			TypeCategory::Long,
			TypeCategory::UnsignedLong,
			TypeCategory::LongLong,
			TypeCategory::UnsignedLongLong,
			TypeCategory::WChar,
			TypeCategory::Char8,
			TypeCategory::Char16,
			TypeCategory::Char32,
			TypeCategory::Float,
			TypeCategory::Double,
			TypeCategory::LongDouble,
		};
		for (TypeCategory builtin_category : builtin_categories) {
			if (getTypeName(builtin_category) == builtin_name) {
				return findNativeType(builtin_category);
			}
		}
		return nullptr;
	};

	if (gTemplateRegistry.lookup_alias_template(template_name).has_value()) {
		AliasTemplateMaterializationResult alias_result =
			materializeAliasTemplateInstantiation(template_name, template_args);
		if (!alias_result.instantiated_name.empty()) {
			normalizePendingSemanticRoots();
			if (alias_result.resolved_type_info == nullptr) {
				alias_result.resolved_type_info =
					findTypeByName(StringTable::getOrInternStringHandle(alias_result.instantiated_name));
			}
			if (alias_result.resolved_type_info == nullptr) {
				alias_result.resolved_type_info =
					resolve_builtin_type_info_by_name(alias_result.instantiated_name);
			}
		}
		return alias_result;
	}

	AliasTemplateMaterializationResult result;
	std::string_view template_name_to_instantiate = template_name;
	result.instantiated_name =
		instantiate_and_register_base_template(template_name_to_instantiate, template_args);
	if (!result.instantiated_name.empty()) {
		normalizePendingSemanticRoots();
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

Parser::AliasTemplateMaterializationResult Parser::resolveCanonicalInstantiatedOwnerForLookup(
	std::string_view owner_name) {
	return resolveCanonicalInstantiatedOwnerForLookup(
		owner_name,
		std::span<const TemplateTypeArg>{});
}

Parser::AliasTemplateMaterializationResult Parser::materializeCanonicalOwnerTypeForLookup(
	const TypeInfo& owner_type_info,
	std::span<const TemplateTypeArg> owner_template_args) {
	AliasTemplateMaterializationResult result;
	result.instantiated_name = StringTable::getStringView(owner_type_info.name());
	result.resolved_type_info = &owner_type_info;

	const auto can_materialize_owner_template =
		[&](std::string_view candidate_name) {
		if (candidate_name.empty()) {
			return false;
		}
		if (gTemplateRegistry.lookup_alias_template(candidate_name).has_value()) {
			return true;
		}
		if (auto template_entry = gTemplateRegistry.lookupTemplate(candidate_name);
			template_entry.has_value() &&
			template_entry->is<TemplateClassDeclarationNode>()) {
			return true;
		}
		return false;
	};

	const TypeInfo* canonical_owner_type_info = &owner_type_info;
	ResolvedAliasTypeInfo resolved_owner_alias = resolveAliasTypeInfo(
		owner_type_info.registeredTypeIndex().withCategory(owner_type_info.typeEnum()));
	if (resolved_owner_alias.terminal_type_info != nullptr &&
		(resolved_owner_alias.terminal_type_info->isTemplateInstantiation() ||
		 resolved_owner_alias.terminal_type_info->isStruct() ||
		 resolved_owner_alias.terminal_type_info->getStructInfo() != nullptr)) {
		canonical_owner_type_info = resolved_owner_alias.terminal_type_info;
	} else if (resolved_owner_alias.type_index.is_valid()) {
		if (const TypeInfo* resolved_owner_index_info =
				tryGetTypeInfo(resolved_owner_alias.type_index);
			resolved_owner_index_info != nullptr &&
			(resolved_owner_index_info->isTemplateInstantiation() ||
			 resolved_owner_index_info->isStruct() ||
			 resolved_owner_index_info->getStructInfo() != nullptr)) {
			canonical_owner_type_info = resolved_owner_index_info;
		}
	}

	result.resolved_type_info = canonical_owner_type_info;
	if (canonical_owner_type_info->name().isValid()) {
		result.instantiated_name =
			StringTable::getStringView(canonical_owner_type_info->name());
	}
	auto canonical_owner_template_names = [&]() {
		StringHandle qualified_base_template_handle =
			gNamespaceRegistry.buildQualifiedIdentifier(
				canonical_owner_type_info->sourceNamespace(),
				canonical_owner_type_info->baseTemplateName());
		std::string_view qualified_base_template_name =
			StringTable::getStringView(qualified_base_template_handle);
		std::string_view base_template_name =
			StringTable::getStringView(canonical_owner_type_info->baseTemplateName());
		return std::tuple<StringHandle, std::string_view, std::string_view>(
			qualified_base_template_handle,
			qualified_base_template_name,
			base_template_name);
	};

	auto try_materialize_exact_owner =
		[&](std::span<const TypeInfo::TemplateArgInfo> stored_args) -> bool {
		std::vector<TemplateTypeArg> concrete_template_args;
		concrete_template_args.reserve(stored_args.size());
		for (const auto& stored_arg : stored_args) {
			TemplateTypeArg concrete_arg = toTemplateTypeArg(stored_arg);
			concrete_arg.setCategory(stored_arg.category());
			if (concrete_arg.is_dependent || stored_arg.dependent_name.isValid()) {
				return false;
			}
			concrete_template_args.push_back(std::move(concrete_arg));
		}
		if (concrete_template_args.empty() && !stored_args.empty()) {
			return false;
		}

		auto [qualified_base_template_handle, qualified_base_template_name, base_template_name] =
			canonical_owner_template_names();
		if (qualified_base_template_name.empty() && base_template_name.empty()) {
			return false;
		}

		AliasTemplateMaterializationResult canonical_owner;
		if (!qualified_base_template_name.empty()) {
			canonical_owner =
				materializeTemplateInstantiationForLookup(
					qualified_base_template_name,
					std::span<const TemplateTypeArg>(
						concrete_template_args.data(),
						concrete_template_args.size()));
		}
		if ((canonical_owner.instantiated_name.empty() &&
			 canonical_owner.resolved_type_info == nullptr) &&
			!base_template_name.empty() &&
			qualified_base_template_handle != canonical_owner_type_info->baseTemplateName()) {
			canonical_owner =
				materializeTemplateInstantiationForLookup(
					base_template_name,
					std::span<const TemplateTypeArg>(
						concrete_template_args.data(),
						concrete_template_args.size()));
		}
		if (canonical_owner.instantiated_name.empty() &&
			canonical_owner.resolved_type_info == nullptr) {
			return false;
		}

		if (!canonical_owner.instantiated_name.empty()) {
			result.instantiated_name = canonical_owner.instantiated_name;
		}
		if (canonical_owner.resolved_type_info != nullptr) {
			result.resolved_type_info = canonical_owner.resolved_type_info;
		}
		return true;
	};

	if (!canonical_owner_type_info->isTemplateInstantiation()) {
		if (!owner_template_args.empty() &&
			can_materialize_owner_template(result.instantiated_name)) {
			AliasTemplateMaterializationResult materialized_owner =
				materializeTemplateInstantiationForLookup(
					result.instantiated_name,
					owner_template_args);
			if (!materialized_owner.instantiated_name.empty() ||
				materialized_owner.resolved_type_info != nullptr) {
				return materialized_owner;
			}
		}
		return result;
	}

	if (try_materialize_exact_owner(canonical_owner_type_info->templateArgs())) {
		return result;
	}

	if (canonical_owner_type_info->hasInstantiationContext() &&
		canonical_owner_type_info->instantiationContext() != nullptr &&
		try_materialize_exact_owner(
			canonical_owner_type_info->instantiationContext()->param_args)) {
		return result;
	}

	auto [qualified_base_template_handle, qualified_base_template_name, base_template_name] =
		canonical_owner_template_names();
	if (!owner_template_args.empty() &&
		(can_materialize_owner_template(qualified_base_template_name) ||
		 can_materialize_owner_template(base_template_name))) {
		AliasTemplateMaterializationResult canonical_owner;
		if (can_materialize_owner_template(qualified_base_template_name)) {
			canonical_owner =
				materializeTemplateInstantiationForLookup(
					qualified_base_template_name,
					owner_template_args);
		}
		if ((canonical_owner.instantiated_name.empty() &&
			 canonical_owner.resolved_type_info == nullptr) &&
			can_materialize_owner_template(base_template_name) &&
			qualified_base_template_handle != canonical_owner_type_info->baseTemplateName()) {
			canonical_owner =
				materializeTemplateInstantiationForLookup(
					base_template_name,
					owner_template_args);
		}
		if (!canonical_owner.instantiated_name.empty() ||
			canonical_owner.resolved_type_info != nullptr) {
			return canonical_owner;
		}
	}

	return result;
}

Parser::AliasTemplateMaterializationResult Parser::materializeCanonicalOwnerTypeForLookup(
	const TemplateTypeArg& owner_type_arg) {
	AliasTemplateMaterializationResult result;
	if (!owner_type_arg.type_index.is_valid()) {
		return result;
	}

	const TypeInfo* owner_type_info = nullptr;
	ResolvedAliasTypeInfo resolved_owner_alias = resolveAliasTypeInfo(
		owner_type_arg.type_index.withCategory(owner_type_arg.typeEnum()));
	if (resolved_owner_alias.terminal_type_info != nullptr) {
		owner_type_info = resolved_owner_alias.terminal_type_info;
	} else if (resolved_owner_alias.type_index.is_valid()) {
		owner_type_info = tryGetTypeInfo(resolved_owner_alias.type_index);
	}
	if (owner_type_info == nullptr) {
		owner_type_info = tryGetTypeInfo(owner_type_arg.type_index);
	}
	if (owner_type_info == nullptr) {
		return result;
	}

	return materializeCanonicalOwnerTypeForLookup(
		*owner_type_info,
		std::span<const TemplateTypeArg>{});
}

Parser::AliasTemplateMaterializationResult Parser::resolveCanonicalInstantiatedOwnerForLookup(
	std::string_view owner_name,
	std::span<const TemplateTypeArg> owner_template_args) {
	AliasTemplateMaterializationResult result;
	result.instantiated_name = owner_name;
	if (owner_name.empty()) {
		return result;
	}

	const auto can_materialize_owner_template =
		[&](std::string_view candidate_name) {
		if (candidate_name.empty()) {
			return false;
		}
		if (gTemplateRegistry.lookup_alias_template(candidate_name).has_value()) {
			return true;
		}
		if (auto template_entry = gTemplateRegistry.lookupTemplate(candidate_name);
			template_entry.has_value() &&
			template_entry->is<TemplateClassDeclarationNode>()) {
			return true;
		}
		return false;
	};

	const StringHandle owner_name_handle = StringTable::getOrInternStringHandle(owner_name);
	if (const TypeInfo* initial_owner_type_info = findTypeByName(owner_name_handle);
		initial_owner_type_info != nullptr) {
		return materializeCanonicalOwnerTypeForLookup(
			*initial_owner_type_info,
			owner_template_args);
	}

	if (!owner_template_args.empty()) {
		if (can_materialize_owner_template(owner_name)) {
			AliasTemplateMaterializationResult materialized_owner =
				materializeTemplateInstantiationForLookup(owner_name, owner_template_args);
			if (!materialized_owner.instantiated_name.empty() ||
				materialized_owner.resolved_type_info != nullptr) {
				return materialized_owner;
			}
		}

		const std::string_view base_template_name = extractBaseTemplateName(owner_name);
		if (!base_template_name.empty() &&
			base_template_name != owner_name &&
			can_materialize_owner_template(base_template_name)) {
			AliasTemplateMaterializationResult materialized_owner =
				materializeTemplateInstantiationForLookup(base_template_name, owner_template_args);
			if (!materialized_owner.instantiated_name.empty() ||
				materialized_owner.resolved_type_info != nullptr) {
				return materialized_owner;
			}
		}
	}

	return result;
}

std::optional<ASTNode> Parser::instantiateLazyMemberForCanonicalOwner(
	std::string_view& owner_name,
	std::string_view member_name,
	std::span<const TemplateTypeArg> owner_template_args) {
	AliasTemplateMaterializationResult canonical_owner =
		resolveCanonicalInstantiatedOwnerForLookup(owner_name, owner_template_args);
	if (!canonical_owner.instantiated_name.empty()) {
		owner_name = canonical_owner.instantiated_name;
	}
	if (owner_name.empty() || member_name.empty()) {
		return std::nullopt;
	}

	StringHandle owner_handle = StringTable::getOrInternStringHandle(owner_name);
	StringHandle member_handle = StringTable::getOrInternStringHandle(member_name);
	LazyMemberKey member_key = LazyMemberKey::anyConst(owner_handle, member_handle);
	std::optional<ASTNode> instantiated = instantiateLazyMemberIfNeeded(member_key);
	if (instantiated.has_value()) {
		FLASH_LOG(
			Templates,
			Debug,
			"Lazy instantiation triggered for canonical owner: ",
			owner_name,
			"::",
			member_name);
		normalizePendingSemanticRoots();
	}
	return instantiated;
}

const TypeInfo* Parser::materializeInstantiatedMemberAliasTarget(
	const TypeSpecifierNode& alias_type_spec,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args) {
	const TypeInfo* original_alias_target_info = tryGetTypeInfo(alias_type_spec.type_index());
	if (!original_alias_target_info) {
		return nullptr;
	}

	if (original_alias_target_info->isDependentMemberType() &&
		original_alias_target_info->hasDependentQualifiedName()) {
		if (const TypeInfo* resolved_dependent_type =
				resolveDependentMemberTypeSemantic(
					*original_alias_target_info,
					template_params,
					template_args,
					StringHandle{});
			resolved_dependent_type != nullptr) {
			return resolved_dependent_type;
		}
	}

	const TypeInfo::DependentQualifiedNameRecord* dependent_name =
		original_alias_target_info->dependentQualifiedName();
	if (dependent_name == nullptr ||
		dependent_name->member_chain.empty()) {
		return nullptr;
	}

	const TypeInfo* dependent_base_info = nullptr;
	if (dependent_name->owner_type.is_valid()) {
		dependent_base_info = tryGetTypeInfo(dependent_name->owner_type);
	}
	if (dependent_base_info == nullptr && dependent_name->owner_name.isValid()) {
		dependent_base_info = findTypeByName(dependent_name->owner_name);
	}
	if (!dependent_base_info || !dependent_base_info->isTemplateInstantiation()) {
		return nullptr;
	}
	std::string_view dependent_base_name =
		StringTable::getStringView(dependent_base_info->name());
	const TypeInfo::DependentQualifiedNameRecord::Member& dependent_member =
		dependent_name->member_chain.back();
	std::string_view dependent_member_name =
		StringTable::getStringView(dependent_member.name);
	StringBuilder member_path_builder;
	for (size_t member_index = 0; member_index < dependent_name->member_chain.size();
		 ++member_index) {
		if (member_index != 0) {
			member_path_builder.append("::");
		}
		member_path_builder.append(
			StringTable::getStringView(
				dependent_name->member_chain[member_index].name));
	}
	std::string_view dependent_member_path = member_path_builder.commit();

	TemplateEnvironment inherited_environment;
	const TemplateEnvironment* inherited_environment_ptr = nullptr;
	if (const TypeInfo::InstantiationContext* dependent_base_context =
			dependent_base_info->instantiationContext();
		dependent_base_context != nullptr &&
		!(dependent_base_info->is_incomplete_instantiation_ &&
		  dependent_base_info->isTemplateInstantiation())) {
		inherited_environment = buildTemplateEnvironment(*dependent_base_context);
		inherited_environment_ptr = &inherited_environment;
	}
	TemplateEnvironment substitution_environment = buildTemplateEnvironment(
		template_params,
		template_args,
		inherited_environment_ptr);
	auto materialize_template_args_with_environment =
		[&](
			std::span<const TypeInfo::TemplateArgInfo> stored_args) -> InlineVector<TemplateTypeArg, 4> {
			InlineVector<TemplateTypeArg, 4> concrete_args;
		concrete_args.reserve(stored_args.size());
		for (const TypeInfo::TemplateArgInfo& stored_arg : stored_args) {
			TemplateTypeArg concrete_arg = toTemplateTypeArg(stored_arg);
			concrete_arg.setCategory(stored_arg.category());
			bool resolved_from_environment = false;
			if (stored_arg.dependent_name.isValid()) {
				std::optional<TemplateTypeArg> bound_arg;
				if (const TemplateTypeArg* direct_bound_arg =
						substitution_environment.findOne(stored_arg.dependent_name);
					direct_bound_arg != nullptr) {
					bound_arg = *direct_bound_arg;
				} else {
					bound_arg = resolveContextBinding(
						stored_arg.dependent_name,
						substitution_environment);
				}
				if (bound_arg.has_value()) {
					if (!stored_arg.is_value && !bound_arg->is_value) {
						concrete_arg = rebindDependentTemplateTypeArg(*bound_arg, stored_arg);
					} else {
						concrete_arg = *bound_arg;
					}
					resolved_from_environment = true;
				}
			}
			if (!resolved_from_environment) {
				concrete_arg = materializeTemplateArg(
					stored_arg,
					template_params,
					template_args,
					[this, substitution_environment](
						const ASTNode& expr,
						std::span<const ASTNode> params,
						std::span<const TemplateTypeArg> args) {
						FlashCpp::ScopedState guard_subs(template_param_substitutions_);
						populateTemplateParamSubstitutions(template_param_substitutions_, substitution_environment);
						return this->evaluateDependentNTTPExpression(expr, params, args);
					});
			}
			concrete_args.push_back(std::move(concrete_arg));
		}
		return concrete_args;
	};
	auto materialize_template_args_from_type_info =
		[&](const TypeInfo& source_type_info) -> InlineVector<TemplateTypeArg, 4> {
		return materialize_template_args_with_environment(source_type_info.templateArgs());
	};
	auto template_args_still_dependent =
		[](std::span<const TemplateTypeArg> args) -> bool {
		for (const TemplateTypeArg& arg : args) {
			if (arg.is_dependent ||
				arg.dependent_name.isValid() ||
				arg.dependent_expr.has_value()) {
				return true;
			}
		}
		return false;
	};

	StringHandle direct_concrete_member_handle =
		StringTable::getOrInternStringHandle(
			StringBuilder()
				.append(dependent_base_name)
				.append("::")
				.append(dependent_member_path)
				.commit());
	auto direct_concrete_member_it = getTypesByNameMap().find(direct_concrete_member_handle);
	if (direct_concrete_member_it != getTypesByNameMap().end() &&
		direct_concrete_member_it->second != nullptr &&
		!(direct_concrete_member_it->second->is_incomplete_instantiation_ &&
		  direct_concrete_member_it->second->isDependentMemberType())) {
		return direct_concrete_member_it->second;
	}

	StringHandle base_template_name_handle = gNamespaceRegistry.buildQualifiedIdentifier(
		dependent_base_info->sourceNamespace(),
		dependent_base_info->baseTemplateName());
	if (dependent_name->owner_name.isValid()) {
		base_template_name_handle = dependent_name->owner_name;
	}
	InlineVector<TemplateTypeArg, 4> concrete_base_args =
		!dependent_name->owner_template_arguments.empty()
			? materialize_template_args_with_environment(
				  dependent_name->owner_template_arguments)
			: materialize_template_args_from_type_info(*dependent_base_info);
	if (template_args_still_dependent(concrete_base_args)) {
		return nullptr;
	}
	AliasTemplateMaterializationResult materialized_alias_base =
		materializeTemplateInstantiationForLookup(
			StringTable::getStringView(base_template_name_handle),
			concrete_base_args);
	if (materialized_alias_base.instantiated_name.empty() &&
		base_template_name_handle != dependent_base_info->baseTemplateName()) {
		materialized_alias_base = materializeTemplateInstantiationForLookup(
			StringTable::getStringView(dependent_base_info->baseTemplateName()),
			concrete_base_args);
	}
	std::string_view materialized_alias_base_name =
		materialized_alias_base.canonicalName();
	if (materialized_alias_base_name.empty()) {
		return nullptr;
	}

	StringHandle member_alias_handle =
		StringTable::getOrInternStringHandle(
			StringBuilder()
				.append(materialized_alias_base_name)
				.append("::")
				.append(dependent_member_path)
				.commit());
	StringHandle materialized_member_alias_handle = member_alias_handle;
	if (!gTemplateRegistry.lookup_alias_template(materialized_member_alias_handle).has_value()) {
		std::string_view inherited_member_alias_name =
			lookup_inherited_member_template_name(
				StringTable::getOrInternStringHandle(materialized_alias_base_name),
				StringTable::getOrInternStringHandle(dependent_member_name),
				0);
		if (!inherited_member_alias_name.empty()) {
			materialized_member_alias_handle =
				StringTable::getOrInternStringHandle(inherited_member_alias_name);
		}
	}
	if (gTemplateRegistry.lookup_alias_template(materialized_member_alias_handle).has_value()) {
		InlineVector<TemplateTypeArg, 4> concrete_member_template_args;
		if (dependent_member.has_template_arguments) {
			concrete_member_template_args =
				materialize_template_args_with_environment(
					dependent_member.template_arguments);
			if (template_args_still_dependent(concrete_member_template_args)) {
				return nullptr;
			}
		} else if (original_alias_target_info->isTemplateInstantiation()) {
			concrete_member_template_args =
				materialize_template_args_from_type_info(*original_alias_target_info);
			if (template_args_still_dependent(concrete_member_template_args)) {
				return nullptr;
			}
		}
		AliasTemplateMaterializationResult materialized_member_alias =
			materializeAliasTemplateInstantiation(
				StringTable::getStringView(materialized_member_alias_handle),
				concrete_member_template_args);
		if (materialized_member_alias.resolved_type_info != nullptr) {
			return materialized_member_alias.resolved_type_info;
		}
	}

	StringHandle concrete_member_handle =
		StringTable::getOrInternStringHandle(
			StringBuilder()
				.append(materialized_alias_base_name)
				.append("::")
				.append(dependent_member_path)
				.commit());
	auto concrete_member_it = getTypesByNameMap().find(concrete_member_handle);
	if (concrete_member_it != getTypesByNameMap().end() &&
		concrete_member_it->second != nullptr &&
		!(concrete_member_it->second->is_incomplete_instantiation_ &&
		  concrete_member_it->second->isDependentMemberType())) {
		return concrete_member_it->second;
	}

	return nullptr;
}

bool Parser::resolveAliasTemplateInstantiation(
	TypeSpecifierNode& type_spec,
	std::string_view alias_template_name,
	std::span<const TemplateTypeArg> template_args) {
	AliasTemplateMaterializationResult materialized_alias =
		materializeAliasTemplateInstantiation(alias_template_name, template_args);
	if (!materialized_alias.resolved_type_info) {
		return false;
	}

	type_spec = resolveTypeInfoToTypeSpec(
		*materialized_alias.resolved_type_info,
		type_spec);
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

	InlineVector<TemplateTypeArg, 4> concrete_args;
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
	std::span<const TemplateTypeArg> template_args) {

	// First check if the base class is a template alias (like bool_constant)
	auto alias_entry = gTemplateRegistry.lookup_alias_template(base_class_name);
	if (alias_entry.has_value()) {
		FLASH_LOG(Parser, Debug, "Base class '", base_class_name, "' is a template alias - resolving");

		const TemplateAliasNode& alias_node = alias_entry->as<TemplateAliasNode>();
		if (!alias_node.is_deferred()) {
			if (std::optional<TemplateTypeArg> rebound_alias_arg =
					tryRebindAliasTargetTemplateArg(alias_node, template_args);
				rebound_alias_arg.has_value() && !rebound_alias_arg->is_value) {
				if (const TypeInfo* rebound_type_info = tryGetTypeInfo(rebound_alias_arg->type_index);
					rebound_type_info != nullptr) {
					base_class_name = StringTable::getStringView(rebound_type_info->name());
					return base_class_name;
				}
				std::string_view rebound_builtin_name = getTypeName(rebound_alias_arg->category());
				if (!rebound_builtin_name.empty()) {
					base_class_name = rebound_builtin_name;
					return base_class_name;
				}
			}

			const TypeSpecifierNode& alias_target_type = alias_node.target_type_node();
			if (const TypeInfo* alias_target_info = tryGetTypeInfo(alias_target_type.type_index());
				alias_target_info != nullptr && alias_target_info->isTemplateInstantiation()) {
				std::vector<TemplateTypeArg> concrete_target_args =
					materializeTemplateArgs(
						*alias_target_info,
						alias_node.template_parameters(),
						template_args,
						[this](
							const ASTNode& expr,
							std::span<const ASTNode> params,
							std::span<const TemplateTypeArg> args) -> std::optional<TemplateTypeArg> {
							return this->evaluateDependentNTTPExpression(expr, params, args);
						});
				StringHandle qualified_target_template_handle =
					gNamespaceRegistry.buildQualifiedIdentifier(
						alias_target_info->sourceNamespace(),
						alias_target_info->baseTemplateName());
				std::string_view target_template_name =
					StringTable::getStringView(qualified_target_template_handle);
				if (target_template_name.empty()) {
					target_template_name =
						StringTable::getStringView(alias_target_info->baseTemplateName());
				}
				if (!target_template_name.empty()) {
					std::string_view mutable_target_name = target_template_name;
					std::string_view instantiated_target_name =
						instantiate_and_register_base_template(
							mutable_target_name,
							concrete_target_args);
					if (!instantiated_target_name.empty()) {
						base_class_name = instantiated_target_name;
						return instantiated_target_name;
					}
				}
			}
		}

		if (alias_node.is_deferred()) {
			auto substituted_args_opt = materializeDeferredAliasTemplateArgs(alias_node, template_args);
			if (!substituted_args_opt.has_value()) {
				return std::string_view();
			}
			InlineVector<TemplateTypeArg, 4> substituted_args = std::move(*substituted_args_opt);

			// Now recursively instantiate the target template
			// The target might itself be a template alias (chain of aliases)
			std::string_view target_name(alias_node.target_template_name());
			std::string_view instantiated_name = instantiate_and_register_base_template(target_name, substituted_args);
			if (!instantiated_name.empty()) {
				if (alias_node.hasDeferredMemberTarget() &&
					!alias_node.targetMemberTemplateArgs().empty()) {
					StringHandle member_alias_handle =
						getDeferredMemberAliasHandle(alias_node, instantiated_name);
					auto member_alias_entry = gTemplateRegistry.lookup_alias_template(member_alias_handle);
					if (auto member_args =
							materializeDeferredAliasMemberTemplateArgs(
								alias_node,
								template_args,
								member_alias_handle)) {
						if (member_alias_entry.has_value()) {
							AliasTemplateMaterializationResult materialized_member =
								materializeAliasTemplateInstantiation(
									StringTable::getStringView(member_alias_handle),
									*member_args);
							std::string_view materialized_member_name =
								materialized_member.canonicalName();
							if (!materialized_member_name.empty()) {
								base_class_name = materialized_member_name;
								return base_class_name;
							}
						} else if (const TypeInfo* dependent_base_info =
								findTypeByName(StringTable::getOrInternStringHandle(instantiated_name));
							dependent_base_info != nullptr &&
							dependent_base_info->is_incomplete_instantiation_ &&
							dependent_base_info->isTemplateInstantiation()) {
							StringHandle placeholder_handle =
								StringTable::getOrInternStringHandle(
									StringBuilder()
										.append(instantiated_name)
										.append("::")
										.append(alias_node.targetMemberTemplateName())
										.append("<")
										.append(member_args->size())
										.append(" args>")
										.commit());
							TypeInfo& placeholder_info = add_empty_type_entry();
							placeholder_info.fallback_size_bits_ = 0;
							placeholder_info.name_ = placeholder_handle;
							placeholder_info.is_incomplete_instantiation_ = true;
							placeholder_info.placeholder_kind_ = DependentPlaceholderKind::DependentMemberType;
							StringHandle member_template_base =
								StringTable::getOrInternStringHandle(
									StringBuilder()
										.append(dependent_base_info->baseTemplateName())
										.append("::")
										.append(alias_node.targetMemberTemplateName())
										.commit());
							placeholder_info.setTemplateInstantiationInfo(
								QualifiedIdentifier::fromQualifiedName(
									StringTable::getStringView(member_template_base),
									gSymbolTable.get_current_namespace_handle()),
								convertToTemplateArgInfo(*member_args));
							getTypesByNameMap()[placeholder_handle] = &placeholder_info;
							base_class_name = StringTable::getStringView(placeholder_handle);
							return base_class_name;
						}
					}
				}
				base_class_name = instantiated_name;
				return instantiated_name;
			}
		}
	}

	// Check if the base class is a template class.  Lookup materialization also
	// routes explicit function-template names through this helper, so non-class
	// templates must report "not a class instantiation" instead of falling into
	// the base-class-only internal error below.
	TemplateNameLookupRequest base_template_lookup_request =
		buildTemplateNameLookupRequest(
			StringTable::getOrInternStringHandle(base_class_name),
			TemplateNameLookupKind::Qualified,
			false);
	TemplateNameLookupResult base_template_lookup =
		gTemplateRegistry.lookupTemplateName(base_template_lookup_request);
	auto template_entry = base_template_lookup.firstDeclarationOfKind(
		TemplateDeclarationKind::ClassTemplate);
	if (template_entry && template_entry->is<TemplateClassDeclarationNode>()) {
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
		auto primary_template_opt = template_entry;
		if (primary_template_opt.has_value() && primary_template_opt->is<TemplateClassDeclarationNode>()) {
			const TemplateClassDeclarationNode& primary_template = primary_template_opt->as<TemplateClassDeclarationNode>();
			const auto& primary_params = primary_template.template_parameters();
			const std::vector<std::string_view> primary_param_names =
				buildTemplateParamNames(primary_params);

			// Fill in defaults for missing arguments
			std::vector<TemplateTypeArg> filled_args(template_args.begin(), template_args.end());
			for (size_t i = filled_args.size(); i < primary_params.size(); ++i) {
				const TemplateParameterNode* param = tryGetTemplateParameterNode(primary_params[i]);
				if (param == nullptr)
					continue;
				if (param->is_variadic())
					continue;
				if (!param->has_default())
					break;

				const ASTNode& default_node = param->default_value();
				if (param->kind() == TemplateParameterKind::Type && default_node.is<TypeSpecifierNode>()) {
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
				} else if (param->kind() == TemplateParameterKind::NonType && default_node.is<ExpressionNode>()) {
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

		throw InternalError("Base class instantiation name should resolve after default filling");
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

					for (const auto& subst : template_param_substitutions_) {
						if (!subst.is_type_param || subst.substituted_type.is_value) {
							continue;
						}
						if (StringTable::getStringView(subst.param_name) != type_name) {
							continue;
						}
						const TemplateTypeArg& arg = subst.substituted_type;
						TypeSpecifierNode new_type(
							arg.typeEnum(),
							TypeQualifier::None,
							get_type_size_bits(arg.category()),
							sizeof_node.sizeof_token(), CVQualifier::None);
						new_type.set_type_index(arg.type_index);
						new_type.set_reference_qualifier(arg.ref_qualifier);
						for (size_t p = 0; p < arg.pointer_depth; ++p) {
							new_type.add_pointer_level(CVQualifier::None);
						}
						auto new_type_node = emplace_node<TypeSpecifierNode>(new_type);
						SizeofExprNode new_sizeof(new_type_node, sizeof_node.sizeof_token());
						return emplace_node<ExpressionNode>(new_sizeof);
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
		const TypeSpecifierNode& ctor_type = ctor.type_node();

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

	// Handle TypeTraitExprNode (e.g. __is_final(T), __is_empty(T))
	// Substitute the template parameter in the type argument so that
	// evaluateDependentNTTPExpression / ConstExpr::Evaluator can produce the
	// correct bool result against the concrete type.
	if (std::holds_alternative<TypeTraitExprNode>(expr_variant)) {
		const TypeTraitExprNode& trait_expr = std::get<TypeTraitExprNode>(expr_variant);
		if (trait_expr.has_type() && trait_expr.type_node().is<TypeSpecifierNode>()) {
			const TypeSpecifierNode& type_node = trait_expr.type_node().as<TypeSpecifierNode>();

			// Try to find the type by its TypeIndex first (most common path)
			auto it = type_substitution_map.find(type_node.type_index());
			if (it == type_substitution_map.end()) {
				// Fallback: match by name when TypeIndex registration differs across templates
				if (type_node.category() == TypeCategory::UserDefined ||
					type_node.category() == TypeCategory::TypeAlias ||
					type_node.category() == TypeCategory::Template) {
					std::string_view type_name = type_node.token().value();
					if (type_name.empty()) {
						if (const TypeInfo* ti = tryGetTypeInfo(type_node.type_index())) {
							type_name = StringTable::getStringView(ti->name());
						}
					}
					for (const auto& [key_type_index, arg] : type_substitution_map) {
						if (const TypeInfo* ki = tryGetTypeInfo(key_type_index)) {
							if (StringTable::getStringView(ki->name()) == type_name) {
								it = type_substitution_map.find(key_type_index);
								break;
							}
						}
					}
				}
			}
			if (it != type_substitution_map.end()) {
				const TemplateTypeArg& arg = it->second;
				TypeSpecifierNode new_type(
					arg.typeEnum(),
					TypeQualifier::None,
					get_type_size_bits(arg.category()),
					type_node.token(), CVQualifier::None);
				new_type.set_type_index(arg.type_index);
				new_type.set_reference_qualifier(arg.ref_qualifier);
				for (size_t p = 0; p < arg.pointer_depth; ++p) {
					new_type.add_pointer_level(CVQualifier::None);
				}
				ASTNode new_type_node = emplace_node<TypeSpecifierNode>(new_type);
				TypeTraitExprNode new_trait(trait_expr.kind(), new_type_node, trait_expr.trait_token());
				return emplace_node<ExpressionNode>(new_trait);
			}
		}
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
		annotateConcreteBinaryOperatorOverload(new_binop);
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
std::optional<ASTNode> Parser::try_instantiate_variable_template(std::string_view template_name, std::span<const TemplateTypeArg> template_args) {
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
				// Resolve type aliases to their underlying concrete types so that
				// e.g. is_same_v<remove_const_t<int>, int> matches the T==T specialization.
				// Only resolve when the type is a plain alias (no outer pointer/ref/cv added by
				// the call site) to avoid merging unrelated qualifiers.
				if (type_info->isTypeAlias() && !arg.is_value && arg.pointer_depth == 0 &&
					arg.ref_qualifier == ReferenceQualifier::None && arg.cv_qualifier == CVQualifier::None) {
					ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(arg.type_index);
					if (resolved_alias.type_index.is_valid() && resolved_alias.type_index != arg.type_index) {
						TemplateTypeArg resolved = makeTemplateTypeArgFromResolvedAlias(resolved_alias, arg.type_index);
						FLASH_LOG(Templates, Debug, "Resolved type alias arg from ", arg.type_index, " to ", resolved.type_index);
						arg = resolved;
					}
				}
				else {
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
		[&](std::span<const TemplateTypeArg> input_args) -> std::optional<std::vector<TemplateTypeArg>> {
		bool has_parameter_pack = false;
		size_t non_variadic_param_count = 0;
		for (const auto& param_node : template_params) {
			const TemplateParameterNode* param = tryGetTemplateParameterNode(param_node);
			if (param == nullptr) {
				continue;
			}
			if (param->is_variadic()) {
				has_parameter_pack = true;
				continue;
			}
			++non_variadic_param_count;
		}

		if (has_parameter_pack) {
			size_t minimum_required_args = 0;
			for (const auto& param_node : template_params) {
				const TemplateParameterNode* param = tryGetTemplateParameterNode(param_node);
				if (param == nullptr) {
					continue;
				}
				if (param->is_variadic() || param->has_default()) {
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
			[&](const TemplateParameterNode& param, std::span<const TemplateTypeArg> bound_args) -> std::optional<TemplateTypeArg> {
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

				ConstExpr::EvaluationContext eval_ctx(gSymbolTable, *this);
				auto eval_result = ConstExpr::Evaluator::evaluate(substituted_default, eval_ctx);
				if (!eval_result.success()) {
					FLASH_LOG(Templates, Error, "Failed to evaluate non-type default for variable template parameter '",
							  param.name(), "'");
					return std::nullopt;
				}

				if (!param.has_type()) {
					return templateTypeArgFromEvalResult(eval_result);
				}
				if (std::optional<TypeSpecifierNode> target_type =
						substituteNonTypeParameterTypeImpl(
							*this,
							param,
							template_params,
							bound_args_inline);
					target_type.has_value()) {
					return templateTypeArgFromEvalResult(eval_result, *target_type);
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
			const TemplateParameterNode* param = tryGetTemplateParameterNode(template_params[i]);
			if (param == nullptr) {
				continue;
			}
			if (param->is_variadic()) {
				size_t remaining_args = arg_index < input_args.size()
					? input_args.size() - arg_index
					: 0;
				size_t required_after = countRequiredTemplateArgsAfter(
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

			auto default_arg = materialize_default_arg(*param, filled_args);
			if (!default_arg.has_value()) {
				FLASH_LOG(Templates, Error, "Variable template '", template_name,
						  "': missing argument for parameter '", param->name(), "'");
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
	std::span<const TemplateTypeArg> filled_args = *filled_args_opt;

	auto try_reparse_variable_template_initializer =
		[&](
			const TemplateVariableDeclarationNode& template_node,
			std::span<const TemplateParameterNode> template_params_for_substitution,
			std::span<const TemplateTypeArg> template_args_for_substitution,
			StringHandle instantiated_variable_name) -> std::optional<ASTNode> {
		const VariableDeclarationNode& pattern_var_decl =
			template_node.variable_decl_node();
		if (!pattern_var_decl.initializer().has_value() ||
			!template_node.has_initializer_replay_position()) {
			return std::nullopt;
		}

		SaveHandle current_pos = save_token_position();
		ScopedLexerPositionRestore lexer_restore(*this, current_pos);

		TemplateInstantiationContext substitution_context =
			buildTemplateInstantiationContext(
				template_params_for_substitution,
				template_args_for_substitution,
				nullptr,
				currentTemplateSubstitutionFailurePolicy());

		TemplateDefinitionLookupContext definition_lookup_context =
			template_node.initializer_definition_lookup_context();
		if (!definition_lookup_context.is_valid()) {
			const DeclarationNode& declaration =
				pattern_var_decl.declaration();
			definition_lookup_context.definition_line =
				declaration.identifier_token().line();
			definition_lookup_context.definition_file_index =
				declaration.identifier_token().file_index();
			definition_lookup_context.definition_namespace =
				gSymbolTable.get_current_namespace_handle();
		}
		definition_lookup_context.current_instantiation_name =
			instantiated_variable_name;
		substitution_context.definition_lookup_context =
			definition_lookup_context.is_valid()
				? &definition_lookup_context
				: nullptr;

		// Capture the caller's depth before forcing replay depth so the guard restores
		// the original instantiation context after this initializer has been reparsed.
		FlashCpp::TemplateDepthGuard guard_template_depth(parsing_template_depth_);
		// Depth 0 means non-template code; depth 1 is the minimum "inside a
		// template declaration" state needed for dependent token classification.
		// Replay must parse as if it were back in the original template declaration
		// regardless of the actual instantiation nesting depth.
		constexpr int kReplayTemplateParsingDepth = 1;
		parsing_template_depth_ = kReplayTemplateParsingDepth;
		ScopedDefinitionLookupContext ctx_scope(
			current_template_definition_lookup_context_,
			substitution_context.definition_lookup_context);

		InlineVector<StringHandle, 4> template_param_names;
		InlineVector<TemplateParameterKind, 4> template_param_kinds;
		InlineVector<TypeCategory, 4> non_type_categories;
		buildVariableTemplateParameterReplayState(
			template_params_for_substitution,
			template_param_names,
			template_param_kinds,
			non_type_categories);
		FlashCpp::ScopedState guard_param_names(currentTemplateParamState());
		setCurrentTemplateParameters(
			template_param_names,
			template_param_kinds,
			non_type_categories);

		restore_lexer_position_only(*template_node.initializer_replay_position());

		DeclarationNode declaration_for_reparse =
			pattern_var_decl.declaration();
		TypeSpecifierNode& type_spec =
			declaration_for_reparse.type_specifier_node();

		std::optional<ASTNode> reparsed_initializer;
		if (peek() == "="_tok) {
			reparsed_initializer = parse_copy_initialization(
				declaration_for_reparse,
				type_spec);
		} else if (peek() == "{"_tok) {
			ParseResult init_result = parse_brace_initializer(type_spec);
			if (!init_result.is_error() &&
				init_result.node().has_value()) {
				reparsed_initializer = *init_result.node();
			}
		}

		if (!reparsed_initializer.has_value()) {
			return std::nullopt;
		}

		return substituteTemplateParameters(
			*reparsed_initializer,
			substitution_context);
	};

	auto try_replay_variable_template_initializer =
		[&](
			const TemplateVariableDeclarationNode& template_node,
			std::span<const TemplateParameterNode> template_params_for_substitution,
			std::span<const TemplateTypeArg> template_args_for_substitution,
			StringHandle instantiated_variable_name,
			std::string_view failure_message) -> std::optional<ASTNode> {
		try {
			return try_reparse_variable_template_initializer(
				template_node,
				template_params_for_substitution,
				template_args_for_substitution,
				instantiated_variable_name);
		} catch (const std::exception& ex) {
			FLASH_LOG_FORMAT(
				Templates,
				Debug,
				"{}{} — falling back to AST substitution",
				failure_message,
				ex.what());
			return std::nullopt;
		}
	};

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
		InlineVector<TemplateTypeArg, 4> converted_args;
		if (!spec_params.empty()) {
			// Build deduced args from the structural match substitutions.
			// TemplatePattern::matches() already deduced T→int by stripping
			// pattern qualifiers, so we use those substitutions directly.
			converted_args.reserve(spec_params.size());
			for (const auto& param : spec_params) {
				if (const TemplateParameterNode* tp = tryGetTemplateParameterNode(param);
					tp != nullptr) {
					auto it = structural_match->substitutions.find(tp->nameHandle());
					if (it != structural_match->substitutions.end()) {
						converted_args.push_back(it->second);
					} else {
						throw InternalError(
							"TemplatePattern::matches() did not produce a substitution for variable-template specialization parameter '" +
							std::string(tp->name()) + "'");
					}
				}
			}
		}

		std::optional<ASTNode> init_expr;
		if (spec_var_decl.initializer().has_value()) {
			if (!spec_params.empty()) {
				StringHandle instantiated_var_handle =
					StringTable::getOrInternStringHandle(persistent_name);
				init_expr = try_replay_variable_template_initializer(
					spec_template,
					spec_params,
					converted_args,
					instantiated_var_handle,
					"Replay substitution failed for variable-template partial specialization initializer: ");
				if (!init_expr.has_value()) {
					init_expr = substituteTemplateParameters(
						*spec_var_decl.initializer(), spec_params, converted_args);
				}
				spec_type = substituteTemplateParameters(
					spec_type, spec_params, converted_args);
			} else {
				StringHandle instantiated_var_handle =
					StringTable::getOrInternStringHandle(persistent_name);
				init_expr = try_replay_variable_template_initializer(
					spec_template,
					spec_params,
					converted_args,
					instantiated_var_handle,
					"Replay parsing failed for variable-template specialization initializer: ");
				if (!init_expr.has_value()) {
					init_expr = *spec_var_decl.initializer();
				}
			}
		} else if (spec_decl.type_specifier_node().category() == TypeCategory::Bool) {
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
		StringHandle instantiated_var_handle =
			StringTable::getOrInternStringHandle(persistent_name);
		new_initializer = try_replay_variable_template_initializer(
			var_template,
			template_params,
			filled_args_inline,
			instantiated_var_handle,
			"Replay substitution failed for variable-template initializer: ");
		if (!new_initializer.has_value()) {
			new_initializer = substituteTemplateParameters(
				orig_var_decl.initializer().value(),
				template_params,
				filled_args_inline);
		}
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
	std::span<const TemplateTypeArg> template_args,
	ASTNode& spec_node) {
	// Generate the instantiated class name
	std::string_view instantiated_name = get_instantiated_class_name(template_name, template_args);
	FLASH_LOG(Templates, Debug, "instantiate_full_specialization called for: ", instantiated_name);

	if (!spec_node.is<StructDeclarationNode>()) {
		FLASH_LOG(Templates, Error, "Full specialization is not a StructDeclarationNode");
		return std::nullopt;
	}

	StructDeclarationNode& spec_struct = spec_node.as<StructDeclarationNode>();
	InlineVector<TemplateParameterNode, 4> no_template_params;

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
			const TypeInfo* alias_semantic_source = tryGetTypeInfo(alias_type_spec.type_index());
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
				alias_semantic_source = concrete_sibling_alias;
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
						no_template_params,
						template_args);
				concrete_member_info != nullptr) {
				alias_target_index =
					concrete_member_info->registeredTypeIndex().withCategory(
						concrete_member_info->typeEnum());
				alias_size_bits = concrete_member_info->sizeInBits().value;
				alias_semantic_source = concrete_member_info;
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
				update_type_alias_copy(
					*alias_info,
					alias_target_index,
					alias_size_bits,
					&alias_registration_type_spec,
					alias_semantic_source);
			} else {
				if (alias_semantic_source != nullptr) {
					alias_info = &add_type_alias_copy(
						qualified_alias_name,
						alias_target_index,
						alias_size_bits,
						alias_registration_type_spec,
						*alias_semantic_source);
				} else {
					alias_info = &add_type_alias_copy(
						qualified_alias_name,
						alias_target_index,
						alias_size_bits,
						alias_registration_type_spec);
				}
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
					nested_alias_spec,
					*original_nested_info);
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
				const TypeInfo* alias_semantic_source =
					tryGetTypeInfo(alias_type_spec.type_index());
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

				TypeInfo& alias_type_info =
					alias_semantic_source != nullptr
						? add_type_alias_copy(
							  qualified_alias_name,
							  alias_target_index,
							  alias_registration_type_spec.size_in_bits(),
							  alias_registration_type_spec,
							  *alias_semantic_source)
						: add_type_alias_copy(
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
		const TypeSpecifierNode& type_spec = decl.type_specifier_node();

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
				static_member.array_dimensions,
				static_member.declaration,
				static_member.initializer_position,
				static_member.is_constexpr);
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

	// Copy base classes from the full specialization. A full specialization fully
	// binds all template parameters, so its base classes refer to concrete types
	// already present in the type table - we just need to mirror them onto the new
	// instantiation so member lookup walks the correct inheritance chain.
	for (const auto& base : spec_struct.base_classes()) {
		const TypeInfo* base_type_info = tryGetTypeInfo(base.type_index);
		if (base_type_info == nullptr) {
			throw InternalError(
				"instantiate_full_specialization: recorded base TypeIndex is invalid for '" +
				std::string(base.name) + "' in '" + std::string(instantiated_name) + "'");
		}
		struct_info->addBaseClass(
			StringTable::getStringView(base_type_info->name()),
			base_type_info->registeredTypeIndex().withCategory(base_type_info->typeEnum()),
			base.access,
			base.is_virtual);
	}

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
	normalizePendingSemanticRoots();

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
	std::span<const TemplateTypeArg> args,
	std::span<const TemplateParameterNode> params) {
	for (size_t i = 0; i < params.size(); ++i) {
		const TemplateParameterNode& tparam = params[i];
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
// Returns the evaluated value and its category if successful, or nullopt if evaluation fails.
std::optional<TemplateTypeArg> Parser::evaluateDependentNTTPExpression(
	const ASTNode& dependent_expr,
	std::span<const ASTNode> template_params,
	std::span<const TemplateTypeArg> template_args) {
	InlineVector<TemplateParameterNode, 4> typed_params =
		collectTemplateParameterNodes(template_params);
	if (typed_params.size() != template_params.size()) {
		throw InternalError(
			"evaluateDependentNTTPExpression expected only TemplateParameterNode entries (found " +
			std::to_string(typed_params.size()) +
			" valid out of " +
			std::to_string(template_params.size()) +
			" total)");
	}
	return evaluateDependentNTTPExpression(
		dependent_expr,
		std::span<const TemplateParameterNode>(typed_params.data(), typed_params.size()),
		template_args);
}

std::optional<TemplateTypeArg> Parser::evaluateDependentNTTPExpression(
	const ASTNode& dependent_expr,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args) {
	// Build type substitution map from template params to args
	std::unordered_map<TypeIndex, TemplateTypeArg> type_substitution_map;
	// Build non-type substitution map for value parameters
	std::unordered_map<std::string_view, int64_t> nontype_substitution_map;
	for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
		const TemplateParameterNode* param = &template_params[i];
		if (param->kind() == TemplateParameterKind::Type) {
			// For typename/class parameters, registered_type_index() is the TypeIndex assigned
			// when the template was parsed (via add_user_type in Parser_Templates_Function.cpp).
			// This is the same TypeIndex that sizeof(T) will carry in its TypeSpecifierNode.
			if (param->registered_type_index().is_valid()) {
				type_substitution_map[param->registered_type_index()] = template_args[i];
			}
			// For non-type parameters that have an explicit type node (e.g., template<int N>
			// where someone uses sizeof(N)), also map by the param's type specifier index.
			if (param->has_type()) {
				const TypeSpecifierNode& param_type = param->type_specifier_node();
				type_substitution_map[param_type.type_index()] = template_args[i];
			}
		} else if (param->kind() == TemplateParameterKind::NonType && template_args[i].is_value) {
			nontype_substitution_map[param->name()] = template_args[i].value;
		}
	}
	for (const auto& subst : template_param_substitutions_) {
		if (subst.is_type_param && !subst.substituted_type.is_value && subst.substituted_type.type_index.is_valid()) {
			auto type_it = getTypesByNameMap().find(subst.param_name);
			if (type_it != getTypesByNameMap().end() && type_it->second != nullptr) {
				TypeIndex param_type_index = type_it->second->registeredTypeIndex();
				if (param_type_index.is_valid()) {
					type_substitution_map[param_type_index] = subst.substituted_type;
				}
			}
		} else if (subst.is_value_param) {
			nontype_substitution_map[StringTable::getStringView(subst.param_name)] = subst.value;
		}
	}

	// Substitute template parameters in the expression to get a concrete AST
	ASTNode substituted = substitute_template_params_in_expression(
		dependent_expr, type_substitution_map, nontype_substitution_map, StringHandle{});

	// Evaluate the substituted expression using the standard constant expression evaluator
	ConstExpr::EvaluationContext eval_ctx(gSymbolTable, *this);
	eval_ctx.template_environment = buildTemplateEnvironment(
		template_params,
		template_args,
		nullptr);
	eval_ctx.template_args.assign(template_args.begin(), template_args.end());
	eval_ctx.template_param_names.reserve(template_params.size());
	for (const TemplateParameterNode& template_param : template_params) {
		eval_ctx.template_param_names.push_back(template_param.name());
	}
	ConstExpr::EvalResult result = ConstExpr::Evaluator::evaluate(substituted, eval_ctx);
	if (result.success()) {
		return templateTypeArgFromEvalResult(result);
	}

	FLASH_LOG(Templates, Debug, "evaluateDependentNTTPExpression: evaluation failed for dependent expression");
	return std::nullopt;
}
