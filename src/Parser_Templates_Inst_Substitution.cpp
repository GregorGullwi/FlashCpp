#include "Parser.h"
#include "ConstExprEvaluator.h"
#include <span>
#include "ExpressionSubstitutor.h"
#include "NameMangling.h"
#include "OverloadResolution.h"
#include "ParserTemplateClassShared.h"
#include "ParserTemplateHelpers.h"
#include "TypeTraitEvaluator.h"

static bool hasComplexDeferredMemberChain(const TemplateAliasNode& node) {
	const auto segments = node.targetMemberTemplateSegments();
	if (segments.size() > 1) {
		return true;
	}
	return std::ranges::any_of(
		segments,
		[](const DeferredAliasMemberTemplateSegment& segment) {
			return segment.has_template_arguments;
		});
}

static const TypeInfo* resolveConcreteAliasSemanticType(const TypeInfo* type_info) {
	const TypeInfo* current_type_info = type_info;
	for (size_t depth = 0; current_type_info != nullptr && depth < 32; ++depth) {
		ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(
			current_type_info->registeredTypeIndex().withCategory(current_type_info->typeEnum()));
		const TypeInfo* terminal_type_info = resolved_alias.terminal_type_info;
		if (terminal_type_info == nullptr || terminal_type_info == current_type_info) {
			break;
		}
		current_type_info = terminal_type_info;
		if (isConcreteAliasSemanticSource(current_type_info)) {
			break;
		}
	}
	return current_type_info;
}

template <typename TemplateParamStorage, typename TemplateArgStorage, typename OnOuterParam>
static bool appendOuterAliasTemplateSubstitutionInputs(
	const OuterTemplateBinding& outer_binding,
	TemplateParamStorage& effective_template_params_storage,
	TemplateArgStorage& effective_template_args_storage,
	OnOuterParam on_outer_param) {
	if (!outer_binding.params.empty()) {
		const size_t pair_count = std::min(
			outer_binding.params.size(),
			outer_binding.param_args.size());
		for (size_t i = 0; i < pair_count; ++i) {
			const TemplateParameterNode* outer_param =
				tryGetTemplateParameterNode(outer_binding.params[i]);
			if (outer_param == nullptr) {
				continue;
			}
			effective_template_params_storage.push_back(*outer_param);
			effective_template_args_storage.push_back(outer_binding.param_args[i]);
			on_outer_param(*outer_param);
		}
		if (!effective_template_args_storage.empty()) {
			return true;
		}
	}

	const size_t fallback_pair_count = std::min(
		outer_binding.param_names.size(),
		outer_binding.param_args.size());
	for (size_t i = 0; i < fallback_pair_count; ++i) {
		Token param_token(
			Token::Type::Identifier,
			StringTable::getStringView(outer_binding.param_names[i]),
			0,
			0,
			0);
		TemplateParameterNode outer_param(
			outer_binding.param_names[i],
			param_token);
		effective_template_params_storage.push_back(outer_param);
		effective_template_args_storage.push_back(outer_binding.param_args[i]);
		on_outer_param(outer_param);
	}
	return !effective_template_args_storage.empty();
}

static void buildEffectiveAliasTemplateSubstitutionInputs(
	const TemplateAliasNode& alias_node,
	const OuterTemplateBinding* outer_binding,
	std::span<const TemplateTypeArg> template_args,
	std::vector<TemplateParameterNode>& effective_template_params_storage,
	std::vector<TemplateTypeArg>& effective_template_args_storage,
	std::span<const TemplateParameterNode>& effective_template_params,
	std::span<const TemplateTypeArg>& effective_template_args) {
	effective_template_params_storage.clear();
	effective_template_args_storage.clear();

	if (outer_binding != nullptr) {
		const size_t pair_count = std::min(
			outer_binding->params.size(),
			outer_binding->param_args.size());
		effective_template_params_storage.reserve(
			pair_count + alias_node.template_parameters().size());
		effective_template_args_storage.reserve(
			pair_count + template_args.size());
		if (!appendOuterAliasTemplateSubstitutionInputs(
				*outer_binding,
				effective_template_params_storage,
				effective_template_args_storage,
				[](const TemplateParameterNode&) {})) {
			effective_template_params_storage.clear();
			effective_template_args_storage.clear();
		}
	} else {
		effective_template_params_storage.reserve(alias_node.template_parameters().size());
		effective_template_args_storage.reserve(template_args.size());
	}

	for (const TemplateParameterNode& template_param : alias_node.template_parameters()) {
		effective_template_params_storage.push_back(template_param);
	}
	effective_template_args_storage.insert(
		effective_template_args_storage.end(),
		template_args.begin(),
		template_args.end());
	effective_template_params = std::span<const TemplateParameterNode>(
		effective_template_params_storage.data(),
		effective_template_params_storage.size());
	effective_template_args = std::span<const TemplateTypeArg>(
		effective_template_args_storage.data(),
		effective_template_args_storage.size());
}

static void buildEffectiveVariableTemplateSubstitutionInputs(
	std::string_view template_name,
	const OuterTemplateBinding* outer_binding,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> filled_args,
	std::vector<TemplateParameterNode>& effective_template_params_storage,
	std::vector<TemplateTypeArg>& effective_template_args_storage,
	std::span<const TemplateParameterNode>& effective_template_params,
	std::span<const TemplateTypeArg>& effective_template_args) {
	effective_template_params = template_params;
	effective_template_args = filled_args;
	if (outer_binding == nullptr) {
		return;
	}
	if (outer_binding->params.empty()) {
		FLASH_LOG_FORMAT(
			Templates,
			Warning,
			"OuterTemplateBinding is missing original parameter metadata while instantiating '{}'; "
			"skipping outer binding merge for variable-template substitution",
			template_name);
		return;
	}

	effective_template_params_storage.clear();
	effective_template_args_storage.clear();
	effective_template_params_storage.reserve(
		outer_binding->params.size() + template_params.size());

	for (const ASTNode& outer_param_node : outer_binding->params) {
		const TemplateParameterNode* outer_param =
			tryGetTemplateParameterNode(outer_param_node);
		if (outer_param == nullptr) {
			continue;
		}
		effective_template_params_storage.push_back(*outer_param);
	}
	if (effective_template_params_storage.empty()) {
		FLASH_LOG_FORMAT(
			Templates,
			Warning,
			"OuterTemplateBinding parameter metadata contains no template parameters while instantiating '{}'; "
			"skipping outer binding merge for variable-template substitution",
			template_name);
		return;
	}
	for (const TemplateParameterNode& param : template_params) {
		effective_template_params_storage.push_back(param);
	}

	const std::span<const TemplateTypeArg> outer_args =
		!outer_binding->all_args.empty()
			? std::span<const TemplateTypeArg>(
				outer_binding->all_args.data(),
				outer_binding->all_args.size())
			: std::span<const TemplateTypeArg>(
				outer_binding->param_args.data(),
				outer_binding->param_args.size());
	effective_template_args_storage.reserve(outer_args.size() + filled_args.size());
	for (const TemplateTypeArg& outer_arg : outer_args) {
		effective_template_args_storage.push_back(outer_arg);
	}
	for (const TemplateTypeArg& arg : filled_args) {
		effective_template_args_storage.push_back(arg);
	}

	effective_template_params = std::span<const TemplateParameterNode>(
		effective_template_params_storage.data(),
		effective_template_params_storage.size());
	effective_template_args = std::span<const TemplateTypeArg>(
		effective_template_args_storage.data(),
		effective_template_args_storage.size());
}

static TemplateParameterNode rebuildOuterTemplateParameterForSubstitution(
	StringHandle param_name,
	const TemplateTypeArg& param_arg) {
	Token param_token(
		Token::Type::Identifier,
		StringTable::getStringView(param_name),
		0,
		0,
		0);
	if (param_arg.is_value) {
		TypeSpecifierNode param_type(
			param_arg.type_index.withCategory(param_arg.typeEnum()),
			get_type_size_bits(param_arg.typeEnum()),
			param_token,
			CVQualifier::None,
			ReferenceQualifier::None);
		return TemplateParameterNode(
			param_name,
			param_type,
			param_token);
	}

	TemplateParameterNode param_node(param_name, param_token);
	if (param_arg.type_index.is_valid()) {
		param_node.set_registered_type_index(
			param_arg.type_index.withCategory(param_arg.typeEnum()));
	}
	return param_node;
}

static void appendMergedOuterTemplateBinding(
	const OuterTemplateBinding& source_binding,
	OuterTemplateBinding& merged_binding) {
	const size_t pair_count = std::min(
		source_binding.param_names.size(),
		source_binding.param_args.size());
	if (pair_count == 0) {
		return;
	}

	size_t overlap = 0;
	const size_t overlap_limit = std::min(merged_binding.param_names.size(), pair_count);
	for (; overlap < overlap_limit; ++overlap) {
		if (merged_binding.param_names[overlap] != source_binding.param_names[overlap]) {
			break;
		}
	}

	for (size_t i = overlap; i < pair_count; ++i) {
		TemplateParameterNode rebuilt_param = rebuildOuterTemplateParameterForSubstitution(
			source_binding.param_names[i],
			source_binding.param_args[i]);
		if (i < source_binding.params.size()) {
			if (const TemplateParameterNode* source_param =
					tryGetTemplateParameterNode(source_binding.params[i]);
				source_param != nullptr) {
				rebuilt_param = *source_param;
			}
		}
		merged_binding.params.push_back(ASTNode::emplace_node<TemplateParameterNode>(rebuilt_param));
		merged_binding.param_names.push_back(source_binding.param_names[i]);
		merged_binding.param_args.push_back(source_binding.param_args[i]);
		merged_binding.all_args.push_back(source_binding.param_args[i]);
	}
}

TemplateTypeArg templateTypeArgFromEvalResult(const ConstExpr::EvalResult& eval_result) {
	if (auto structural_identity = ConstExpr::makeStructuralClassValueIdentity(eval_result)) {
		return TemplateTypeArg::makeValueIdentity(*structural_identity);
	}
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
	// Supports type-side `Type...` and expression-side forwarding patterns.
	// Note: parser recovery may represent `Vs...` as a plain identifier expression `Vs`
	// in deferred alias targets, so identifier/TTP-reference spellings are also treated
	// as potential forwarding candidates and validated against variadic parameter metadata.
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
		if (const auto* identifier = std::get_if<IdentifierNode>(&target_arg_expr)) {
			return identifier->name();
		}
		if (const auto* tparam_ref = std::get_if<TemplateParameterReferenceNode>(&target_arg_expr)) {
			return StringTable::getStringView(tparam_ref->param_name());
		}
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

	template <typename EvaluateDependentExprFn>
	std::optional<TemplateTypeArg> substituteAndEvaluateNonTypeDefaultImpl(
		Parser& parser,
		const ASTNode& default_node,
		const InlineVector<TemplateParameterNode, 4>& template_params,
		std::span<const TemplateTypeArg> template_args,
		std::span<const std::string_view> template_param_names,
		EvaluateDependentExprFn&& evaluate_dependent_expr) {
		(void)template_param_names;
	ASTNode substituted_default_node = substituteNonTypeDefaultExpressionImpl(
		parser,
		default_node,
		template_params,
		template_args);
	if (!substituted_default_node.is<ExpressionNode>()) {
		return std::nullopt;
	}

	std::optional<TemplateTypeArg> evaluated_default =
		evaluate_dependent_expr(
			substituted_default_node,
			std::span<const TemplateParameterNode>(template_params.data(), template_params.size()),
			template_args);
	if (!evaluated_default.has_value()) {
		FLASH_LOG(Templates, Trace, "substituteAndEvaluateNonTypeDefaultImpl: evaluation failed");
		return std::nullopt;
	}

	FLASH_LOG(Templates, Trace, "substituteAndEvaluateNonTypeDefaultImpl: succeeded");
	if (template_args.size() < template_params.size()) {
		const TemplateParameterNode& param = template_params[template_args.size()];
		if (std::optional<TypeSpecifierNode> target_type =
				substituteNonTypeParameterTypeImpl(
					parser,
					param,
					template_params,
					template_args);
			target_type.has_value()) {
			TemplateTypeArg coerced_default = *evaluated_default;
			const TypeCategory target_category = target_type->type();
			TypeIndex target_type_index =
				target_type->type_index().withCategory(target_category);
			if (!target_type_index.is_valid() && is_builtin_type(target_category)) {
				target_type_index = nativeTypeIndex(target_category);
			}
			if (!target_type_index.is_valid()) {
				target_type_index = TypeIndex{0, target_category};
			}
			if (coerced_default.is_value && isIntegralType(target_category)) {
				coerced_default.value = convertIntegralTemplateValueToType(
					coerced_default.value,
					target_category);
			}
			coerced_default.type_index = TemplateTypeArg::makeTypeIndex(target_type_index);
			return coerced_default;
		}
	}
	return evaluated_default;
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
		std::span<const std::string_view>(derived_param_names.data(), derived_param_names.size()),
		[this](
			const ASTNode& expr,
			std::span<const TemplateParameterNode> params,
			std::span<const TemplateTypeArg> args) {
			return evaluateDependentNTTPExpression(expr, params, args);
		});
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
		template_param_names,
		[this](
			const ASTNode& expr,
			std::span<const TemplateParameterNode> params,
			std::span<const TemplateTypeArg> args) {
			return evaluateDependentNTTPExpression(expr, params, args);
		});
}

std::string_view Parser::get_instantiated_class_name(std::string_view template_name, std::span<const TemplateTypeArg> template_args) {
	std::span<const TemplateTypeArg> effective_template_args = template_args;
	std::vector<TemplateTypeArg> effective_template_args_storage;
	if (size_t last_colon = template_name.rfind("::"); last_colon != std::string_view::npos) {
		std::string_view owner_name = template_name.substr(0, last_colon);
		bool is_nested_member_class_template = false;
		size_t raw_param_count = 0;
		if (auto template_opt = gTemplateRegistry.lookupTemplate(template_name);
			template_opt.has_value() &&
			template_opt->is<TemplateClassDeclarationNode>()) {
			raw_param_count =
				template_opt->as<TemplateClassDeclarationNode>()
					.template_parameters()
					.size();
			if (auto owner_template_opt = gTemplateRegistry.lookupTemplate(owner_name);
				owner_template_opt.has_value() &&
				owner_template_opt->is<TemplateClassDeclarationNode>()) {
				is_nested_member_class_template = true;
			}
		}

		if (is_nested_member_class_template &&
			template_args.size() <= raw_param_count) {
			const OuterTemplateBinding* outer_binding =
				gTemplateRegistry.getOuterTemplateBinding(template_name);
			std::optional<OuterTemplateBinding> synthesized_outer_binding;
			if (outer_binding == nullptr) {
				synthesized_outer_binding = buildOuterBindingForOwner(
					StringTable::getOrInternStringHandle(owner_name));
				if ((!synthesized_outer_binding.has_value() ||
					 synthesized_outer_binding->param_names.empty()) &&
					current_instantiation_ctx_ != nullptr &&
					current_instantiation_ctx_->origin_name.isValid()) {
					synthesized_outer_binding = buildOuterBindingForOwner(
						current_instantiation_ctx_->origin_name);
				}
				if (!synthesized_outer_binding.has_value() ||
					synthesized_outer_binding->param_names.empty()) {
					StringHandle contextual_owner{};
					if (!struct_parsing_context_stack_.empty()) {
						contextual_owner = StringTable::getOrInternStringHandle(
							struct_parsing_context_stack_.back().struct_name);
					} else if (!member_function_context_stack_.empty()) {
						contextual_owner = member_function_context_stack_.back().struct_name;
					}
					if (contextual_owner.isValid()) {
						synthesized_outer_binding = buildOuterBindingForOwner(
							contextual_owner);
					}
				}
				if (synthesized_outer_binding.has_value() &&
					!synthesized_outer_binding->param_names.empty()) {
					outer_binding = &*synthesized_outer_binding;
				}
			}

			if (outer_binding != nullptr &&
				(!outer_binding->all_args.empty() ||
				 !outer_binding->param_args.empty())) {
				const std::span<const TemplateTypeArg> outer_args =
					!outer_binding->all_args.empty()
						? std::span<const TemplateTypeArg>(
							outer_binding->all_args.data(),
							outer_binding->all_args.size())
						: std::span<const TemplateTypeArg>(
							outer_binding->param_args.data(),
							outer_binding->param_args.size());
				effective_template_args_storage.reserve(
					outer_args.size() + template_args.size());
				for (const TemplateTypeArg& outer_arg : outer_args) {
					effective_template_args_storage.push_back(outer_arg);
				}
				for (const TemplateTypeArg& template_arg : template_args) {
					effective_template_args_storage.push_back(template_arg);
				}
				effective_template_args = std::span<const TemplateTypeArg>(
					effective_template_args_storage.data(),
					effective_template_args_storage.size());
			}
		}
	}

	if (size_t last_colon = template_name.rfind("::"); last_colon != std::string_view::npos) {
		template_name = template_name.substr(last_colon + 2);
	}
	auto result = FlashCpp::generateInstantiatedNameFromArgs(
		template_name,
		effective_template_args);
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
		if (!normalized.is_value &&
			!normalized.type_index.is_valid()) {
			TypeIndex native_index = nativeTypeIndex(normalized.category());
			if (native_index.is_valid()) {
				normalized.type_index = native_index.withCategory(normalized.category());
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
				auto materialize_args = [&](const TypeInfo& source_type_info) {
					return materializeTemplateArgs(
						source_type_info,
						template_parameters,
						template_args,
						[this, owner_name = source_type_info.name()](
							StringHandle dependent_name,
							const ASTNode& expr,
							std::span<const ASTNode> params,
							std::span<const TemplateTypeArg> args) -> std::optional<TemplateTypeArg> {
							InlineVector<TemplateParameterNode, 4> typed_params =
								collectTemplateParameterNodes(params);
							if (dependent_name.isValid()) {
								if (auto folded = tryFoldDependentQualifiedStaticMemberNTTP(
										dependent_name,
										std::span<const TemplateParameterNode>(
											typed_params.data(), typed_params.size()),
										args);
									folded.has_value()) {
									return folded;
								}
							}
							return this->evaluateDependentNTTPExpression(
								expr,
								std::span<const TemplateParameterNode>(
									typed_params.data(),
									typed_params.size()),
								args,
								owner_name);
						});
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
		FLASH_LOG(Templates, Trace, "materializeDeferredAliasTemplateArg: skipping early eval for TypeTraitExprNode");

		// If the type arguments that feed this TypeTraitExpr are still dependent
		// (e.g. __is_final(H) where H is an outer template parameter of HeadBase),
		// evaluating the trait on the placeholder type would silently return false
		// and poison the cached instantiation. Instead, return a dependent bool
		// placeholder so the downstream code registers a dependent Cond<dep,...>
		// rather than a concrete (wrong) one. When the outer template is later
		// instantiated with a concrete type (e.g. FinalHead), this function will
		// be called again with non-dependent args and will evaluate correctly.
		const auto unresolved_dependent_anchor = [this](const TemplateTypeArg& candidate) -> StringHandle {
			if (candidate.dependent_name.isValid()) {
				return candidate.dependent_name;
			}
			if (!candidate.is_value &&
				candidate.type_index.is_valid()) {
				if (const TypeInfo* type_info = tryGetTypeInfo(candidate.type_index);
					type_info != nullptr &&
					type_info->name().isValid() &&
					(typeIndexContainsDependentPlaceholder(candidate.type_index) ||
					 type_info->isDependentPlaceholder() ||
					 type_info->isTemplatePlaceholder() ||
					 type_info->is_incomplete_instantiation_ ||
					 is_template_parameter(
						 StringTable::getStringView(type_info->name())))) {
					return type_info->name();
				}
			}
			return {};
		};
		for (const TemplateTypeArg& arg : template_args) {
			if (StringHandle dependent_anchor = unresolved_dependent_anchor(arg);
				dependent_anchor.isValid()) {
				FLASH_LOG(Templates, Trace, "materializeDeferredAliasTemplateArg: arg is dependent, returning dependent bool placeholder");
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
			FLASH_LOG(Templates, Trace, "materializeDeferredAliasTemplateArg: sizeof...(", pack_name, ") = ", *pack_size, " (counted from template_parameters/args)");
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

std::optional<TemplateArgumentVector> Parser::materializeDeferredAliasTemplateArgs(
	const TemplateAliasNode& alias_node,
	std::span<const TemplateTypeArg> template_args,
	const OuterTemplateBinding* outer_binding) {
	TemplateArgumentVector substituted_args;
	InlineVector<TemplateParameterNode, 4> effective_template_parameters;
	InlineVector<StringHandle, 4> effective_param_names;
	TemplateArgumentVector effective_template_args;
	if (outer_binding != nullptr) {
		const size_t pair_count = std::min(
			outer_binding->params.size(),
			outer_binding->param_args.size());
		effective_template_parameters.reserve(pair_count + alias_node.template_parameters().size());
		effective_param_names.reserve(pair_count + alias_node.template_param_names().size());
		effective_template_args.reserve(pair_count + template_args.size());
		if (!appendOuterAliasTemplateSubstitutionInputs(
				*outer_binding,
				effective_template_parameters,
				effective_template_args,
				[&](const TemplateParameterNode& outer_param) {
					effective_param_names.push_back(outer_param.nameHandle());
				})) {
			// Outer binding has a stale/unresolvable parameter node.
			// Fall back to ignoring the outer binding and use only the alias's own parameters.
			// This handles cases like std::bool_constant<false> used as a base class outside
			// the template context where the alias was originally parsed.
			effective_template_parameters.clear();
			effective_param_names.clear();
			effective_template_args.clear();
			outer_binding = nullptr;
		}
		for (const TemplateParameterNode& alias_param : alias_node.template_parameters()) {
			effective_template_parameters.push_back(alias_param);
		}
		for (StringHandle alias_param_name : alias_node.template_param_names()) {
			effective_param_names.push_back(alias_param_name);
		}
		for (const TemplateTypeArg& template_arg : template_args) {
			effective_template_args.push_back(template_arg);
		}
	}
	const auto& param_names = outer_binding != nullptr
		? effective_param_names
		: alias_node.template_param_names();
	std::span<const TemplateTypeArg> substitution_args = outer_binding != nullptr
		? std::span<const TemplateTypeArg>(effective_template_args.data(), effective_template_args.size())
		: template_args;
	std::span<const ASTNode> target_template_args = alias_node.target_template_args();
	std::span<const TemplateParameterNode> alias_params_span(
		outer_binding != nullptr ? effective_template_parameters.data() : alias_node.template_parameters().data(),
		outer_binding != nullptr ? effective_template_parameters.size() : alias_node.template_parameters().size());
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
				substitution_args.size());
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
			size_t expected_pack_count = pack_range->second;
			std::optional<std::string_view> pack_name =
				tryGetAliasPackForwardingName(target_arg_node);
			bool is_non_type_pack = false;
			TypeCategory pack_value_category = TypeCategory::Int;
			if (pack_name.has_value()) {
				for (const TemplateParameterNode& alias_pack_param : alias_params_span) {
					if (alias_pack_param.name() != *pack_name) {
						continue;
					}
					is_non_type_pack =
						alias_pack_param.kind() == TemplateParameterKind::NonType;
					if (alias_pack_param.has_type()) {
						pack_value_category =
							alias_pack_param.type_specifier_node().type();
					}
					break;
				}
			}
			if (is_non_type_pack && expected_pack_count <= 1) {
				if (auto tracked_pack_size = get_template_param_pack_size(*pack_name);
					tracked_pack_size.has_value() && *tracked_pack_size > expected_pack_count) {
					expected_pack_count = *tracked_pack_size;
				} else if (auto active_pack_size = get_pack_size(*pack_name);
						   active_pack_size.has_value() && *active_pack_size > expected_pack_count) {
					expected_pack_count = *active_pack_size;
				} else if (size_t counted_pack_elements = count_pack_elements(*pack_name);
						   counted_pack_elements > expected_pack_count) {
					expected_pack_count = counted_pack_elements;
				}
			}
			for (size_t offset = 0; offset < expected_pack_count; ++offset) {
				const size_t arg_index = pack_range->first + offset;
				if (arg_index >= substitution_args.size()) {
					if (!is_non_type_pack) {
						break;
					}
					const std::string_view pack_element_name =
						StringBuilder()
							.append(*pack_name)
							.append("_")
							.append(offset)
							.commit();
					StringHandle pack_element_handle =
						StringTable::getOrInternStringHandle(pack_element_name);
					ASTNode pack_element_expr =
						ASTNode::emplace_node<ExpressionNode>(
							IdentifierNode(
								Token(Token::Type::Identifier, pack_element_name, 0, 0, 0)));
					if (auto const_value =
							try_evaluate_constant_expression(pack_element_expr);
						const_value.has_value()) {
						TemplateTypeArg recovered_arg =
							TemplateTypeArg::makeValueIdentity(const_value->identity);
						substituted_args.push_back(std::move(recovered_arg));
						continue;
					}
					TemplateTypeArg dependent_pack_arg =
						TemplateTypeArg::makeDependentValue(
							pack_element_handle,
							pack_value_category);
					substituted_args.push_back(std::move(dependent_pack_arg));
					continue;
				}
				substituted_args.push_back(substitution_args[arg_index]);
			}
			continue;
		}
		auto materialized_arg = materializeDeferredAliasTemplateArg(
			target_arg_node,
			outer_binding != nullptr ? effective_template_parameters : alias_node.template_parameters(),
			param_names,
			substitution_args,
			target_template_param);
		if (!materialized_arg.has_value()) {
			return std::nullopt;
		}
		substituted_args.push_back(std::move(*materialized_arg));
	}

	FLASH_LOG(
		Parser,
		Debug,
		"materializeDeferredAliasTemplateArgs: alias='",
		alias_node.alias_name(),
		"', target='",
		alias_node.target_template_name(),
		"', input_args=",
		template_args.size(),
		", output_args=",
		substituted_args.size());
	for (size_t i = 0; i < substituted_args.size(); ++i) {
		const TemplateTypeArg& arg = substituted_args[i];
		const TypeInfo* arg_type_info =
			(!arg.is_value && arg.type_index.is_valid())
				? tryGetTypeInfo(arg.type_index)
				: nullptr;
		FLASH_LOG(
			Parser,
			Debug,
			"  deferred_arg[",
			i,
			"]: is_value=",
			arg.is_value ? 1 : 0,
			", is_dep=",
			arg.is_dependent ? 1 : 0,
			", type=",
			static_cast<int>(arg.category()),
			", type_name='",
			(arg_type_info != nullptr
				 ? StringTable::getStringView(arg_type_info->name())
				 : std::string_view()),
			"', ref=",
			static_cast<int>(arg.ref_qualifier),
			", ptr=",
			static_cast<int>(arg.pointer_depth),
			", cv=",
			static_cast<int>(arg.cv_qualifier));
	}

	return substituted_args;
}

StringHandle Parser::getDeferredMemberAliasHandle(
	StringHandle member_name,
	std::string_view instantiated_name,
	bool require_owner_qualified) {
	StringHandle member_alias_handle =
		StringTable::getOrInternStringHandle(
			StringBuilder()
				.append(instantiated_name)
				.append("::")
				.append(member_name.view())
				.commit());
	if (gTemplateRegistry.lookup_alias_template(member_alias_handle).has_value()) {
		return member_alias_handle;
	}
	std::string_view inherited_member_alias_name =
		lookup_inherited_member_template_name(
			StringTable::getOrInternStringHandle(instantiated_name),
			member_name,
			0);
	if (!inherited_member_alias_name.empty()) {
		return StringTable::getOrInternStringHandle(inherited_member_alias_name);
	}
	if (require_owner_qualified) {
		return {};
	}
	return member_name;
}

std::optional<QualifiedTypeMemberAccess> Parser::materializeDeferredAliasMemberTemplateSegment(
	const TemplateAliasNode& alias_node,
	const DeferredAliasMemberTemplateSegment& segment,
	std::span<const TemplateTypeArg> template_args,
	StringHandle owner_qualified_handle) {
	const auto member_template_params = getTargetTemplateParameters(owner_qualified_handle);
	TemplateArgumentVector member_args;
	std::span<const ASTNode> member_template_args(segment.template_args.data(), segment.template_args.size());
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

	QualifiedTypeMemberAccess member_access;
	member_access.member_name = segment.name;
	member_access.has_template_arguments = segment.has_template_arguments;
	if (segment.has_template_arguments) {
		std::vector<TemplateTypeArg> stored_args(member_args.begin(), member_args.end());
		member_access.template_arguments =
			&gChunkedAnyStorage.emplace_back<std::vector<TemplateTypeArg>>(std::move(stored_args));
		member_access.template_argument_infos =
			build_template_arg_infos(*member_access.template_arguments, member_template_args);
	}
	return member_access;
}

const TypeInfo* Parser::materializeDeferredAliasMemberTemplateChain(
	const TemplateAliasNode& alias_node,
	std::span<const TemplateTypeArg> template_args,
	std::string_view instantiated_name) {
	std::vector<QualifiedTypeMemberAccess> member_chain;
	member_chain.reserve(alias_node.targetMemberTemplateSegments().size());
	std::string progressive_qualified_name(instantiated_name);
	for (const DeferredAliasMemberTemplateSegment& segment : alias_node.targetMemberTemplateSegments()) {
		StringHandle owner_qualified_handle =
			getDeferredMemberAliasHandle(
				segment.name,
				std::string_view(progressive_qualified_name),
				segment.has_template_arguments);
		auto materialized_segment =
			materializeDeferredAliasMemberTemplateSegment(
				alias_node,
				segment,
				template_args,
				owner_qualified_handle);
		if (!materialized_segment.has_value()) {
			return nullptr;
		}
		member_chain.push_back(std::move(*materialized_segment));
		progressive_qualified_name.append("::");
		progressive_qualified_name.append(segment.name.view());
	}
	return resolveBaseClassMemberTypeChain(
		instantiated_name,
		std::span<QualifiedTypeMemberAccess>(
			member_chain.data(),
			member_chain.size()));
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

std::optional<size_t> Parser::findDirectAliasTargetParameterIndex(
	const TemplateAliasNode& alias_node) const {
	if (alias_node.is_deferred()) {
		return std::nullopt;
	}

	const TypeSpecifierNode& alias_target = alias_node.target_type_node();
	const TypeInfo* alias_target_info = tryGetTypeInfo(alias_target.type_index());
	if (alias_target_info != nullptr &&
		(alias_target_info->isTemplateInstantiation() ||
		 alias_target_info->isDependentMemberType() ||
		 alias_target_info->hasDependentQualifiedName())) {
		return std::nullopt;
	}

	StringHandle alias_target_name = getAliasTargetNameHandle(alias_target);
	if (!alias_target_name.isValid()) {
		return std::nullopt;
	}

	const auto& alias_param_names = alias_node.template_param_names();
	for (size_t alias_param_index = 0;
		 alias_param_index < alias_param_names.size();
		 ++alias_param_index) {
		if (alias_param_names[alias_param_index] == alias_target_name) {
			return alias_param_index;
		}
	}
	return std::nullopt;
}

std::optional<size_t> Parser::findAliasTargetTemplateParamIndex(
	const TemplateAliasNode& alias_node,
	std::span<const TemplateTypeArg> concrete_args) const {
	std::optional<size_t> alias_param_index =
		findDirectAliasTargetParameterIndex(alias_node);
	if (!alias_param_index.has_value() ||
		*alias_param_index >= concrete_args.size()) {
		return std::nullopt;
	}
	return alias_param_index;
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

TypeSpecifierNode Parser::buildDependentAliasTemplateTypeSpecifier(
	std::string_view alias_name,
	const TemplateAliasNode& alias_node,
	std::span<const TemplateTypeArg> template_args,
	const Token& source_token,
	CVQualifier cv_qualifier) {
	// Dependent NTTPs currently hash like placeholder value 0, so a concrete
	// ConditionalT<false, ...> can occupy the same mangled name. Always keep
	// call-site dependent args on a distinct DependentArgs placeholder.
	StringHandle dependent_alias_handle =
		StringTable::getOrInternStringHandle(
			StringBuilder()
				.append(get_instantiated_class_name(alias_name, template_args))
				.append("#dep")
				.commit());
	const TypeInfo* dependent_alias_info = findTypeByName(dependent_alias_handle);
	const bool existing_is_reusable_dependent_args =
		dependent_alias_info != nullptr &&
		dependent_alias_info->isTemplateInstantiation() &&
		dependent_alias_info->placeholder_kind_ == DependentPlaceholderKind::DependentArgs &&
		dependent_alias_info->templateArgs().size() == template_args.size();
	if (dependent_alias_info != nullptr && !existing_is_reusable_dependent_args) {
		dependent_alias_handle = StringTable::getOrInternStringHandle(
			StringBuilder()
				.append(StringTable::getStringView(dependent_alias_handle))
				.append("#")
				.append(std::to_string(
					static_cast<unsigned long long>(
						reinterpret_cast<uintptr_t>(&alias_node))))
				.commit());
		dependent_alias_info = findTypeByName(dependent_alias_handle);
	}
	if (dependent_alias_info == nullptr) {
		TypeInfo& placeholder_type = add_empty_type_entry();
		placeholder_type.fallback_size_bits_ = 0;
		placeholder_type.name_ = dependent_alias_handle;
		placeholder_type.is_incomplete_instantiation_ = true;
		placeholder_type.placeholder_kind_ =
			DependentPlaceholderKind::DependentArgs;
		InlineVector<StringHandle, 4> alias_param_names;
		for (const TemplateParameterNode& param : alias_node.template_parameters()) {
			alias_param_names.push_back(param.nameHandle());
		}
		InlineVector<TypeInfo::TemplateArgInfo, 4> alias_args =
			toTemplateArgInfoList(template_args);
		placeholder_type.setTemplateInstantiationInfo(
			QualifiedIdentifier::fromQualifiedName(
				alias_name,
				gSymbolTable.get_current_namespace_handle()),
			alias_args);
		placeholder_type.setInstantiationContext(
			std::move(alias_param_names),
			alias_args,
			nullptr);
		getTypesByNameMap()[dependent_alias_handle] = &placeholder_type;
		dependent_alias_info = &placeholder_type;
	}

	if (!dependent_alias_info->isTemplateInstantiation()) {
		throw InternalError(
			"Dependent alias-template placeholder collided with a non-template type");
	}

	return TypeSpecifierNode(
		dependent_alias_info->registeredTypeIndex().withCategory(
			TypeCategory::Template),
		0,
		source_token,
		cv_qualifier,
		ReferenceQualifier::None);
}

TypeSpecifierNode Parser::buildDependentDirectAliasTypeSpecifier(
	std::string_view alias_name,
	const TemplateAliasNode& alias_node,
	std::span<const TemplateTypeArg> template_args,
	const Token& source_token,
	CVQualifier cv_qualifier) {
	if (!findDirectAliasTargetParameterIndex(alias_node).has_value()) {
		throw InternalError(
			"Dependent direct-alias placeholder requested for a non-direct alias target");
	}
	return buildDependentAliasTemplateTypeSpecifier(
		alias_name,
		alias_node,
		template_args,
		source_token,
		cv_qualifier);
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
	TemplateArgumentVector& template_args) {
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
	TemplateArgumentVector& template_args) {
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
	std::optional<TypeSpecifierNode> resolved_deferred_decltype_spec;
	const TemplateAliasNode* alias_node = nullptr;
	if (auto alias_entry = gTemplateRegistry.lookup_alias_template(alias_template_name);
		alias_entry.has_value() && alias_entry->is<TemplateAliasNode>()) {
		alias_node = &alias_entry->as<TemplateAliasNode>();
	}
	const OuterTemplateBinding* outer_binding =
		gTemplateRegistry.getOuterTemplateBinding(alias_template_name);
	std::vector<TemplateParameterNode> effective_template_params_storage;
	std::vector<TemplateTypeArg> effective_template_args_storage;
	std::span<const TemplateParameterNode> effective_template_params;
	std::span<const TemplateTypeArg> effective_template_args;
	if (alias_node != nullptr) {
		buildEffectiveAliasTemplateSubstitutionInputs(
			*alias_node,
			outer_binding,
			template_args,
			effective_template_params_storage,
			effective_template_args_storage,
			effective_template_params,
			effective_template_args);
	}
	auto alias_preserves_surface_type = [](const ResolvedAliasTypeInfo& resolved_alias) {
		return resolved_alias.cv_qualifier != CVQualifier::None ||
			   resolved_alias.pointer_depth != 0 ||
			   resolved_alias.reference_qualifier != ReferenceQualifier::None ||
			   resolved_alias.function_signature.has_value() ||
			   resolved_alias.member_class_name.has_value() ||
			   !resolved_alias.array_dimensions.empty();
	};
	auto tryResolveDeferredDecltypeAliasTarget = [&]() -> bool {
		if (alias_node == nullptr) {
			return false;
		}
		const TypeInfo* target_type_info =
			tryGetTypeInfo(alias_node->target_type_node().type_index());
		if (target_type_info == nullptr) {
			return false;
		}
		const ASTNode* deferred_decltype_expr =
			target_type_info->deferredDecltypeExpression();
		if (deferred_decltype_expr == nullptr) {
			return false;
		}

		std::optional<TemplateEnvironment> outer_environment;
		if (outer_binding != nullptr) {
			outer_environment = buildTemplateEnvironment(*outer_binding);
		}
		TemplateInstantiationContext substitution_context =
			buildTemplateInstantiationContext(
				effective_template_params,
				effective_template_args,
				outer_environment.has_value() ? &*outer_environment : nullptr,
				currentTemplateSubstitutionFailurePolicy());
		ExpressionSubstitutor substitutor(substitution_context, *this);
		ASTNode substituted_expr = substitutor.substitute(*deferred_decltype_expr);
		auto type_spec_opt = get_expression_type(substituted_expr);
		if (!type_spec_opt.has_value()) {
			if (substituted_expr.is<ExpressionNode>() &&
				std::holds_alternative<CallExprNode>(
					substituted_expr.as<ExpressionNode>())) {
				const CallExprNode& substituted_call =
					std::get<CallExprNode>(
						substituted_expr.as<ExpressionNode>());
				if (substituted_call.has_qualified_name() &&
					!substituted_call.has_dependent_unqualified_lookup_record() &&
					!substituted_call.has_dependent_qualified_lookup_record() &&
					!substituted_call.has_definition_lookup_record() &&
					!substituted_call.has_parser_return_type_hint()) {
					throw CompileError(
						std::string(
							StringBuilder()
								.append("No matching function for call to '")
								.append(substituted_call.qualified_name())
								.append("'")
								.commit()));
				}
			}
			return false;
		}

		resolved_deferred_decltype_spec = *type_spec_opt;
		const TypeSpecifierNode& resolved_type_spec =
			*resolved_deferred_decltype_spec;
		const TypeInfo* resolved_type_info =
			tryGetTypeInfo(resolved_type_spec.type_index());
		if (resolved_type_info == nullptr) {
			TypeIndex native_index =
				nativeTypeIndex(resolved_type_spec.type());
			if (native_index.is_valid()) {
				resolved_type_info =
					tryGetTypeInfo(native_index.withCategory(
						resolved_type_spec.type()));
			}
		}
		if (resolved_type_info == nullptr) {
			return false;
		}

		result.resolved_type_info = resolved_type_info;
		result.instantiated_name =
			StringTable::getStringView(resolved_type_info->name());
		return true;
	};
	auto tryResolveDirectAliasTarget = [&]() -> bool {
		if (alias_node == nullptr) {
			return false;
		}
		if (alias_node->is_deferred()) {
			return false;
		}
		if (const TypeInfo* alias_target_info =
				tryGetTypeInfo(alias_node->target_type_node().type_index());
			alias_target_info != nullptr &&
			(alias_target_info->isTemplateInstantiation() ||
			 alias_target_info->isDependentMemberType() ||
			 alias_target_info->hasDependentQualifiedName())) {
			return false;
		}
		std::optional<TemplateTypeArg> rebound_arg =
			tryRebindAliasTargetTemplateArg(*alias_node, template_args);
		// If rebinding via the alias's own template params failed, the alias target
		// may be an *outer* template parameter (e.g. `template<typename U> using Apply = T`
		// where T comes from an enclosing class template's outer binding T=int).
		// Look it up in the stored outer binding before giving up.
		if (!rebound_arg.has_value() && outer_binding != nullptr) {
			StringHandle alias_target_name =
				getAliasTargetNameHandle(alias_node->target_type_node());
			if (alias_target_name.isValid()) {
				for (size_t i = 0;
					 i < outer_binding->param_names.size() &&
					 i < outer_binding->param_args.size();
					 ++i) {
					if (outer_binding->param_names[i] == alias_target_name) {
						rebound_arg = outer_binding->param_args[i];
						break;
					}
				}
			}
		}
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
		auto materialize_args = [&](const TypeInfo& nested_source_type_info) {
			return materializeTemplateArgs(
				nested_source_type_info,
				effective_template_params,
				effective_template_args,
			[this, owner_name = nested_source_type_info.name()](
				StringHandle dependent_name,
				const ASTNode& expr,
				std::span<const ASTNode> params,
				std::span<const TemplateTypeArg> args) -> std::optional<TemplateTypeArg> {
			InlineVector<TemplateParameterNode, 4> typed_params =
				collectTemplateParameterNodes(params);
			if (dependent_name.isValid()) {
				if (auto folded = tryFoldDependentQualifiedStaticMemberNTTP(
						dependent_name,
						std::span<const TemplateParameterNode>(
							typed_params.data(), typed_params.size()),
						args);
					folded.has_value()) {
					return folded;
				}
			}
			return this->evaluateDependentNTTPExpression(
						expr,
						std::span<const TemplateParameterNode>(
							typed_params.data(),
							typed_params.size()),
						args,
						owner_name);
				});
		};
		std::vector<TemplateTypeArg> concrete_args =
			materialize_args(source_type_info);
		TemplateEnvironment substitution_environment = buildTemplateEnvironment(
			effective_template_params,
			effective_template_args,
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
		if (templateArgsStillNeedAliasLookupMaterialization(concrete_target_args)) {
			// Preserve the alias boundary while its target still contains an
			// unresolved nested template argument.  Returning the target's raw
			// placeholder here loses the alias parameter-to-outer-argument
			// relationship between an outer argument and an intermediate alias, so a
			// later concrete rematerialization can still replace the outer argument.
			TypeSpecifierNode dependent_alias_spec =
				buildDependentAliasTemplateTypeSpecifier(
					alias_template_name,
					*alias_node,
					template_args,
					alias_node->target_type_node().token(),
					alias_node->target_type_node().cv_qualifier());
			const TypeInfo* dependent_alias_info =
				tryGetTypeInfo(dependent_alias_spec.type_index());
			if (dependent_alias_info != nullptr) {
				result.resolved_type_info = dependent_alias_info;
				result.instantiated_name =
					StringTable::getStringView(dependent_alias_info->name());
				return true;
			}
		}
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
			if (!tryResolveDeferredDecltypeAliasTarget()) {
				(void)tryMaterializeTemplateAliasTarget();
			}
		}
		if (alias_node != nullptr &&
			result.resolved_type_info == nullptr &&
			result.instantiated_name.empty()) {
			if (const TypeInfo* concrete_member_alias =
					materializeInstantiatedMemberAliasTarget(
						alias_node->target_type_node(),
						effective_template_params,
						effective_template_args);
				concrete_member_alias != nullptr) {
				result.resolved_type_info = concrete_member_alias;
				result.instantiated_name =
					StringTable::getStringView(concrete_member_alias->name());
			}
		}
		if (result.resolved_type_info != nullptr) {
			if (const TypeInfo* concrete_type_info =
					resolveConcreteAliasSemanticType(result.resolved_type_info);
				concrete_type_info != nullptr) {
				result.resolved_type_info = concrete_type_info;
				result.instantiated_name = StringTable::getStringView(concrete_type_info->name());
			}
		}
		return result;
	}

	result.resolved_type_info =
		findTypeByName(StringTable::getOrInternStringHandle(result.instantiated_name));
	if (result.resolved_type_info == nullptr &&
		resolved_deferred_decltype_spec.has_value()) {
		tryResolveDeferredDecltypeAliasTarget();
	}
	if (alias_node != nullptr &&
		result.resolved_type_info != nullptr &&
		!resolved_deferred_decltype_spec.has_value()) {
		(void)tryResolveDeferredDecltypeAliasTarget();
	}
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
		alias_node->is_deferred()) {
		if (const TypeInfo* concrete_deferred_target =
				materializeInstantiatedMemberAliasTarget(
					alias_node->target_type_node(),
					effective_template_params,
					effective_template_args);
			concrete_deferred_target != nullptr) {
			const TypeInfo* resolved_deferred_target = concrete_deferred_target;
			ResolvedAliasTypeInfo resolved_deferred_alias = resolveAliasTypeInfo(
				concrete_deferred_target->registeredTypeIndex().withCategory(
					concrete_deferred_target->typeEnum()));
			if (resolved_deferred_alias.terminal_type_info != nullptr &&
				isConcreteAliasSemanticSource(resolved_deferred_alias.terminal_type_info)) {
				resolved_deferred_target = resolved_deferred_alias.terminal_type_info;
			}
			if (isConcreteAliasSemanticSource(resolved_deferred_target)) {
				result.resolved_type_info = resolved_deferred_target;
				result.instantiated_name =
					StringTable::getStringView(resolved_deferred_target->name());
			}
		}
	}
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
				materializeDeferredAliasTemplateArgs(*alias_node, template_args, outer_binding);
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
		hasComplexDeferredMemberChain(*alias_node) &&
		!result.instantiated_name.empty()) {
		if (const TypeInfo* materialized_member =
				materializeDeferredAliasMemberTemplateChain(
					*alias_node,
					template_args,
					result.instantiated_name);
			materialized_member != nullptr) {
			result.instantiated_name = StringTable::getStringView(materialized_member->name());
			result.resolved_type_info = materialized_member;
		}
	}
	if (alias_node != nullptr &&
		result.resolved_type_info != nullptr) {
		// Prefer a direct alias target over the helper instantiation itself.
		// Member-alias targets are checked after this because an alias target like
		// `Owner<B>::template type<T, F>` must first select `Owner<B>` and then
		// instantiate the selected member alias with its own `<T, F>` arguments.
		// Do not gate this on templateArgsStillNeedAliasLookupMaterialization:
		// NTTP expressions such as `is_simple_alloc_v<Alloc>` may still carry a
		// dependent_expr marker even after the selected branch is concrete, and
		// skipping `::type` leaves the `conditional` specialization itself as the
		// alias target (breaking later `Selected::value_type` lookup).
		tryResolveDirectAliasTarget();
		if (const TypeInfo* concrete_member_alias =
				materializeInstantiatedMemberAliasTarget(
					alias_node->target_type_node(),
					effective_template_params,
					effective_template_args);
			concrete_member_alias != nullptr) {
			bool member_alias_has_dependent_args = false;
			for (const auto& arg_info : concrete_member_alias->templateArgs()) {
				TemplateTypeArg arg = toTemplateTypeArg(arg_info);
				if (arg.is_dependent ||
					arg.dependent_name.isValid() ||
					arg.dependent_expr.has_value() ||
					(!arg.is_value &&
					 arg.type_index.is_valid() &&
					 typeIndexContainsDependentPlaceholder(arg.type_index))) {
					member_alias_has_dependent_args = true;
					break;
				}
			}
			if (!member_alias_has_dependent_args) {
				const TypeInfo* resolved_member_info = concrete_member_alias;
				ResolvedAliasTypeInfo resolved_member_alias = resolveAliasTypeInfo(
					concrete_member_alias->registeredTypeIndex().withCategory(
						concrete_member_alias->typeEnum()));
				if (resolved_member_alias.terminal_type_info != nullptr &&
					isConcreteAliasSemanticSource(resolved_member_alias.terminal_type_info)) {
					resolved_member_info = resolved_member_alias.terminal_type_info;
				}
				result.resolved_type_info = resolved_member_info;
				result.instantiated_name =
					StringTable::getStringView(resolved_member_info->name());
			}
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
		ResolvedAliasTypeInfo resolved_alias_before_registration = resolveAliasTypeInfo(
			resolved_type_info.registeredTypeIndex().withCategory(
				resolved_type_info.typeEnum()));
		FLASH_LOG(
			Parser,
			Debug,
			"Alias registration source: alias='",
			alias_template_name,
			"', resolved='",
			StringTable::getStringView(resolved_type_info.name()),
			"', type=",
			static_cast<int>(resolved_type_info.typeEnum()),
			", type_index=",
			resolved_type_info.registeredTypeIndex().index(),
			", terminal='",
			(resolved_alias_before_registration.terminal_type_info != nullptr
				 ? StringTable::getStringView(
					   resolved_alias_before_registration.terminal_type_info->name())
				 : std::string_view()),
			"'");
		TypeIndex alias_target_index =
			resolved_type_info.registeredTypeIndex().withCategory(
				resolved_type_info.typeEnum());
		const TypeSpecifierNode& alias_target_type_spec =
			alias_node->target_type_node();
		const bool alias_target_preserves_surface =
			alias_target_type_spec.cv_qualifier() != CVQualifier::None ||
			alias_target_type_spec.reference_qualifier() != ReferenceQualifier::None ||
			alias_target_type_spec.pointer_depth() != 0 ||
			alias_target_type_spec.has_function_signature() ||
			alias_target_type_spec.is_array();
		TypeSpecifierNode alias_registration_type_spec =
			resolved_deferred_decltype_spec.value_or(alias_target_type_spec);
		if (resolved_deferred_decltype_spec.has_value()) {
			alias_registration_type_spec = *resolved_deferred_decltype_spec;
		} else if (alias_target_preserves_surface) {
			alias_registration_type_spec =
				resolveTypeInfoToTypeSpec(
					resolved_type_info,
					alias_target_type_spec);
		} else {
			alias_registration_type_spec =
				resolveTypeInfoToTypeSpec(
					resolved_type_info,
					alias_target_type_spec);
			if (!alias_registration_type_spec.type_index().is_valid()) {
				alias_registration_type_spec.set_type_index(
					alias_target_index.withCategory(resolved_type_info.typeEnum()));
				alias_registration_type_spec.set_size_in_bits(resolved_type_info.sizeInBits());
			}
		}

		auto registerOrUpdateAlias = [&](StringHandle alias_handle) {
			TypeInfo* alias_type_info = nullptr;
			const TypeInfo* alias_semantic_source =
				isConcreteAliasSemanticSource(&resolved_type_info)
					? &resolved_type_info
					: nullptr;
			auto existing_alias_it = getTypesByNameMap().find(alias_handle);
			if (existing_alias_it != getTypesByNameMap().end() &&
				existing_alias_it->second != nullptr) {
				alias_type_info = existing_alias_it->second;
				update_type_alias_copy(
					*alias_type_info,
					alias_target_index,
					resolved_type_info.sizeInBits().value,
					&alias_registration_type_spec,
					alias_semantic_source);
			} else if (!is_qualified_alias_template) {
				if (alias_semantic_source != nullptr) {
					alias_type_info = &add_type_alias_copy(
						alias_handle,
						alias_target_index,
						resolved_type_info.sizeInBits().value,
						alias_registration_type_spec,
						*alias_semantic_source);
				} else {
					alias_type_info = &add_type_alias_copy(
						alias_handle,
						alias_target_index,
						resolved_type_info.sizeInBits().value,
						alias_registration_type_spec);
				}
			}
			return alias_type_info;
		};

		TypeInfo* alias_instantiated_type_info =
			registerOrUpdateAlias(alias_instantiated_handle);
		if (alias_instantiated_handle.view() != unqualified_alias_instantiated_name) {
			registerOrUpdateAlias(
				StringTable::getOrInternStringHandle(unqualified_alias_instantiated_name));
		}
		if (alias_instantiated_type_info != nullptr) {
			result.resolved_type_info = alias_instantiated_type_info;
			result.instantiated_name =
				StringTable::getStringView(alias_instantiated_handle);
		}
	}
	if (result.resolved_type_info != nullptr) {
		if (const TypeInfo* concrete_type_info =
				resolveConcreteAliasSemanticType(result.resolved_type_info);
			concrete_type_info != nullptr) {
			result.resolved_type_info = concrete_type_info;
			result.instantiated_name = StringTable::getStringView(concrete_type_info->name());
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

	auto [qualified_base_template_handle, qualified_base_template_name, base_template_name] =
		canonical_owner_template_names();
	if (!owner_template_args.empty() &&
		(can_materialize_owner_template(qualified_base_template_name) ||
		 can_materialize_owner_template(base_template_name))) {
		AliasTemplateMaterializationResult requested_owner;
		if (!qualified_base_template_name.empty()) {
			requested_owner =
				materializeTemplateInstantiationForLookup(
					qualified_base_template_name,
					owner_template_args);
		}
		if ((requested_owner.instantiated_name.empty() &&
			 requested_owner.resolved_type_info == nullptr) &&
			!base_template_name.empty()) {
			requested_owner =
				materializeTemplateInstantiationForLookup(
					base_template_name,
					owner_template_args);
		}
		if (!requested_owner.instantiated_name.empty() ||
			requested_owner.resolved_type_info != nullptr) {
			return requested_owner;
		}
	}

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
			canonical_owner_type_info->instantiationContext()->param_args())) {
		return result;
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

OuterTemplateBinding Parser::buildAccumulatedOuterTemplateBinding(
	const TypeInfo* owner_type_info,
	const OuterTemplateBinding* inherited_binding,
	StringHandle fallback_binding_lookup_name) {
	OuterTemplateBinding binding;
	auto overlay_binding =
		[&](StringHandle param_name, const TemplateTypeArg& param_arg) {
		for (size_t i = 0; i < binding.param_names.size();) {
			if (binding.param_names[i] == param_name) {
				binding.param_names.erase(binding.param_names.begin() + i);
				binding.param_args.erase(binding.param_args.begin() + i);
				continue;
			}
			++i;
		}
		binding.param_names.push_back(param_name);
		binding.param_args.push_back(param_arg);
	};
	auto overlay_existing_binding =
		[&](const OuterTemplateBinding* existing_binding) {
		if (existing_binding == nullptr) {
			return;
		}
		for (size_t i = 0;
			 i < existing_binding->param_names.size() &&
			 i < existing_binding->param_args.size();
			 ++i) {
			overlay_binding(
				existing_binding->param_names[i],
				existing_binding->param_args[i]);
		}
	};

	StringHandle owner_base_binding_handle;
	if (owner_type_info != nullptr &&
		owner_type_info->sourceNamespace().isValid() &&
		owner_type_info->baseTemplateName().isValid()) {
		owner_base_binding_handle =
			gNamespaceRegistry.buildQualifiedIdentifier(
				owner_type_info->sourceNamespace(),
				owner_type_info->baseTemplateName());
	}

	if (inherited_binding != nullptr) {
		overlay_existing_binding(inherited_binding);
	} else {
		if (owner_base_binding_handle.isValid()) {
			overlay_existing_binding(
				gTemplateRegistry.getOuterTemplateBinding(
					StringTable::getStringView(owner_base_binding_handle)));
		}
		if (fallback_binding_lookup_name.isValid() &&
			fallback_binding_lookup_name != owner_base_binding_handle) {
			overlay_existing_binding(
				gTemplateRegistry.getOuterTemplateBinding(
					StringTable::getStringView(fallback_binding_lookup_name)));
		}
	}

	if (owner_type_info != nullptr &&
		owner_type_info->hasInstantiationContext()) {
		const TypeInfo::InstantiationContext* instantiation_context =
			owner_type_info->instantiationContext();
		for (size_t i = 0;
			 instantiation_context != nullptr &&
			 i < instantiation_context->param_names.size() &&
			 i < instantiation_context->param_args().size();
			 ++i) {
			overlay_binding(
				instantiation_context->param_names[i],
				toTemplateTypeArg(instantiation_context->param_args()[i]));
		}
	}

	return binding;
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

std::optional<TemplateTypeArg> Parser::tryFoldDependentQualifiedStaticMemberNTTPFromExpression(
	const ExpressionNode& expression,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args,
	bool require_owner_type_argument) {
	const ExpressionNode* inner_expression = &expression;
	if (const auto* cast_node = std::get_if<StaticCastNode>(inner_expression);
		cast_node != nullptr &&
		cast_node->expr().is<ExpressionNode>()) {
		inner_expression = &cast_node->expr().as<ExpressionNode>();
	}
	const auto* qualified_identifier = std::get_if<QualifiedIdentifierNode>(inner_expression);
	if (qualified_identifier == nullptr || !qualified_identifier->nameHandle().isValid()) {
		return std::nullopt;
	}
	std::string_view owner_name = gNamespaceRegistry.getQualifiedName(
		qualified_identifier->namespace_handle());
	if (owner_name.empty()) {
		return std::nullopt;
	}
	if (require_owner_type_argument) {
		const TypeInfo* owner_info = findTypeByName(StringTable::getOrInternStringHandle(owner_name));
		bool owner_has_type_argument = false;
		if (owner_info != nullptr) {
			for (const TypeInfo::TemplateArgInfo& owner_arg : owner_info->templateArgs()) {
				if (!owner_arg.is_value) {
					owner_has_type_argument = true;
					break;
				}
			}
		}
		if (!owner_has_type_argument) {
			return std::nullopt;
		}
	}
	StringHandle dependent_member_name = StringTable::getOrInternStringHandle(
		StringBuilder()
			.append(owner_name)
			.append("::")
			.append(qualified_identifier->nameHandle())
			.commit());
	return tryFoldDependentQualifiedStaticMemberNTTP(
		dependent_member_name,
		template_params,
		template_args);
}

std::optional<TemplateTypeArg> Parser::tryFoldDependentQualifiedStaticMemberNTTP(
	StringHandle dependent_name,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args) {
	if (!dependent_name.isValid()) {
		return std::nullopt;
	}
	std::string_view full_name = StringTable::getStringView(dependent_name);
	const size_t sep = full_name.rfind("::");
	if (sep == std::string_view::npos || sep == 0 || sep + 2 >= full_name.size()) {
		return std::nullopt;
	}
	std::string_view owner_name = full_name.substr(0, sep);
	std::string_view member_name = full_name.substr(sep + 2);
	StringHandle owner_handle = StringTable::getOrInternStringHandle(owner_name);
	const TypeInfo* owner_info = findTypeByName(owner_handle);
	if (owner_info == nullptr) {
		return std::nullopt;
	}
	if (owner_info->isTemplateInstantiation()) {
		std::vector<TemplateTypeArg> active_type_args;
		for (size_t index = 0;
			index < template_params.size() && index < template_args.size();
			++index) {
			if (template_params[index].kind() == TemplateParameterKind::Type &&
				!template_args[index].is_value) {
				active_type_args.push_back(template_args[index]);
			}
		}
		const auto& stored_owner_args = owner_info->templateArgs();
		bool stored_args_are_types = !stored_owner_args.empty();
		for (const TypeInfo::TemplateArgInfo& stored_arg : stored_owner_args) {
			if (stored_arg.is_value) {
				stored_args_are_types = false;
				break;
			}
		}
		if (stored_args_are_types &&
			active_type_args.size() == stored_owner_args.size()) {
			const StringHandle base_template_name = owner_info->baseTemplateName();
			if (base_template_name.isValid()) {
				AliasTemplateMaterializationResult active_owner =
					materializeTemplateInstantiationForLookup(
						StringTable::getStringView(base_template_name),
						active_type_args);
				const TypeInfo* active_owner_info = active_owner.resolved_type_info;
				if (active_owner_info == nullptr &&
					!active_owner.instantiated_name.empty()) {
					active_owner_info = findTypeByName(
						StringTable::getOrInternStringHandle(
							active_owner.instantiated_name));
				}
				if (active_owner_info != nullptr && active_owner_info != owner_info) {
					owner_info = active_owner_info;
				}
			}
		}
	}
	ResolvedAliasTypeInfo resolved_owner_alias = resolveAliasTypeInfo(
		owner_info->registeredTypeIndex().withCategory(owner_info->typeEnum()));
	if (resolved_owner_alias.terminal_type_info != nullptr &&
		resolved_owner_alias.terminal_type_info != owner_info) {
		StringHandle terminal_member_name = StringTable::getOrInternStringHandle(
			StringBuilder()
				.append(resolved_owner_alias.terminal_type_info->name())
				.append("::")
				.append(member_name)
				.commit());
		if (auto folded = tryFoldDependentQualifiedStaticMemberNTTP(
				terminal_member_name,
				template_params,
				template_args);
			folded.has_value()) {
			return folded;
		}
	}

	if (owner_info->isTemplateInstantiation() ||
		owner_info->isDependentPlaceholder() ||
		owner_info->is_incomplete_instantiation_) {
		auto evaluate_owner_arg =
			[this, owner_info, owner_name = owner_info->name()](
				StringHandle dependent_name,
				const ASTNode& expr,
				std::span<const ASTNode> params,
				std::span<const TemplateTypeArg> args) -> std::optional<TemplateTypeArg> {
			if (dependent_name.isValid()) {
				InlineVector<TemplateParameterNode, 4> typed_params =
					collectTemplateParameterNodes(params);
				if (auto folded = tryFoldDependentQualifiedStaticMemberNTTP(
						dependent_name,
						std::span<const TemplateParameterNode>(
							typed_params.data(), typed_params.size()),
						args);
					folded.has_value()) {
					return folded;
				}
			}
			InlineVector<TemplateParameterNode, 4> typed_params =
				collectTemplateParameterNodes(params);
			std::vector<TemplateTypeArg> extended_eval_args;
			if (expr.is<ExpressionNode>() &&
				std::holds_alternative<TypeTraitExprNode>(expr.as<ExpressionNode>())) {
				const TypeTraitExprNode& trait =
					std::get<TypeTraitExprNode>(expr.as<ExpressionNode>());
				if (trait.has_type() && trait.type_node().is<TypeSpecifierNode>()) {
					const TypeSpecifierNode& operand =
						trait.type_node().as<TypeSpecifierNode>();
					StringHandle operand_name = operand.token().handle();
					bool operand_is_bound = false;
					for (const TemplateParameterNode& param : typed_params) {
						if (param.nameHandle() == operand_name) {
							operand_is_bound = true;
							break;
						}
					}
					const TemplateTypeArg* sole_type_arg = nullptr;
					bool ambiguous_type_arg = false;
					for (size_t arg_index = 0;
						 arg_index < typed_params.size() && arg_index < args.size();
						 ++arg_index) {
						bool is_owner_parameter = false;
						if (owner_info->hasInstantiationContext()) {
							TemplateEnvironment owner_environment = buildTemplateEnvironment(
								*owner_info->instantiationContext());
							for (const TemplateBinding& owner_binding : owner_environment.bindings) {
								if (owner_binding.name == typed_params[arg_index].nameHandle()) {
									is_owner_parameter = true;
									break;
								}
							}
						}
						if (is_owner_parameter ||
							typed_params[arg_index].kind() != TemplateParameterKind::Type ||
							args[arg_index].is_value) {
							continue;
						}
						if (sole_type_arg != nullptr) {
							ambiguous_type_arg = true;
							break;
						}
						sole_type_arg = &args[arg_index];
					}
					if (!operand_is_bound &&
						operand_name.isValid() &&
						!ambiguous_type_arg &&
						sole_type_arg != nullptr) {
						typed_params.push_back(
							TemplateParameterNode(operand_name, operand.token()));
						extended_eval_args.assign(args.begin(), args.end());
						extended_eval_args.push_back(*sole_type_arg);
						if (!trait.has_second_type() && !trait.is_variadic_trait()) {
							TypeSpecifierNode concrete_operand =
								makeTypeSpecifierFromTemplateTypeArg(
									*sole_type_arg,
									operand.token());
							ASTNode directly_substituted =
								ASTNode::emplace_node<ExpressionNode>(
									TypeTraitExprNode(
										trait.kind(),
										ASTNode::emplace_node<TypeSpecifierNode>(
											std::move(concrete_operand)),
										trait.trait_token()));
							if (auto value = try_evaluate_constant_expression(
									directly_substituted);
								value.has_value()) {
								return TemplateTypeArg(value->value, value->type);
							}
						}
						args = std::span<const TemplateTypeArg>(
							extended_eval_args.data(), extended_eval_args.size());
					}
				}
			}
			return this->evaluateDependentNTTPExpression(
				expr,
				std::span<const TemplateParameterNode>(
					typed_params.data(), typed_params.size()),
				args,
				owner_name);
		};
		std::vector<TemplateTypeArg> concrete_owner_args =
			materializeTemplateArgs(
				*owner_info,
				template_params,
				template_args,
				evaluate_owner_arg);
		std::vector<TemplateTypeArg> active_type_args;
		for (size_t active_index = 0;
			 active_index < template_params.size() && active_index < template_args.size();
			 ++active_index) {
			if (template_params[active_index].kind() == TemplateParameterKind::Type &&
				!template_args[active_index].is_value) {
				active_type_args.push_back(template_args[active_index]);
			}
		}
		for (TemplateTypeArg& concrete_owner_arg : concrete_owner_args) {
			if (concrete_owner_arg.is_value ||
				!concrete_owner_arg.type_index.is_valid()) {
				continue;
			}
			const TypeInfo* nested_info = tryGetTypeInfo(concrete_owner_arg.type_index);
			if (nested_info == nullptr ||
				!nested_info->isTemplateInstantiation() ||
				!nested_info->baseTemplateName().isValid() ||
				nested_info->templateArgs().size() != active_type_args.size()) {
				continue;
			}
			bool nested_arg_needs_materialization =
				nested_info->isDependentPlaceholder() ||
				nested_info->is_incomplete_instantiation_ ||
				typeIndexContainsDependentPlaceholder(concrete_owner_arg.type_index);
			if (!nested_arg_needs_materialization) {
				continue;
			}
			AliasTemplateMaterializationResult materialized_nested =
				materializeTemplateInstantiationForLookup(
					StringTable::getStringView(nested_info->baseTemplateName()),
					active_type_args);
			if (materialized_nested.resolved_type_info != nullptr) {
				TypeIndex resolved_index =
					materialized_nested.resolved_type_info->registeredTypeIndex().withCategory(
						materialized_nested.resolved_type_info->typeEnum());
				concrete_owner_arg = makeTemplateTypeArgFromResolvedAlias(
					resolveAliasTypeInfo(resolved_index),
					resolved_index);
			}
		}
		bool owner_args_still_dependent = false;
		for (const TemplateTypeArg& owner_arg : concrete_owner_args) {
			if (owner_arg.is_dependent ||
				owner_arg.dependent_name.isValid() ||
				owner_arg.dependent_expr.has_value()) {
				owner_args_still_dependent = true;
				break;
			}
		}
		if (owner_args_still_dependent) {
			return std::nullopt;
		}
		StringHandle base_template_name = owner_info->baseTemplateName();
		if (owner_info->sourceNamespace().isValid() && base_template_name.isValid()) {
			base_template_name = gNamespaceRegistry.buildQualifiedIdentifier(
				owner_info->sourceNamespace(),
				base_template_name);
		}
		std::string_view base_template_name_view =
			StringTable::getStringView(base_template_name);
		if (base_template_name_view.empty()) {
			return std::nullopt;
		}
		AliasTemplateMaterializationResult materialized_owner =
			materializeTemplateInstantiationForLookup(
				base_template_name_view,
				concrete_owner_args);
		if (materialized_owner.resolved_type_info != nullptr) {
			owner_info = materialized_owner.resolved_type_info;
		} else if (!materialized_owner.instantiated_name.empty()) {
			if (const TypeInfo* resolved_owner = findTypeByName(
					StringTable::getOrInternStringHandle(
						materialized_owner.instantiated_name));
				resolved_owner != nullptr) {
				owner_info = resolved_owner;
			}
		}
		std::optional<ASTNode> instantiated_owner = try_instantiate_class_template(
			base_template_name_view,
			concrete_owner_args,
			false);
		if (instantiated_owner.has_value() &&
			instantiated_owner->is<StructDeclarationNode>()) {
			owner_info = findTypeByName(
				instantiated_owner->as<StructDeclarationNode>().name());
		}
		StringHandle concrete_owner_handle = StringTable::getOrInternStringHandle(
			get_instantiated_class_name(base_template_name_view, concrete_owner_args));
		if (owner_info == nullptr ||
			owner_info->isDependentPlaceholder() ||
			owner_info->is_incomplete_instantiation_) {
			if (const TypeInfo* concrete_owner = findTypeByName(concrete_owner_handle);
				concrete_owner != nullptr) {
				owner_info = concrete_owner;
			}
		}
	}

	auto tryFoldInheritedAliasMember = [&]() -> std::optional<TemplateTypeArg> {
		if (owner_info == nullptr) {
			return std::nullopt;
		}
		if (const TypeInfo* inherited_type = lookup_inherited_type_alias(
				owner_info->name(),
				StringTable::getOrInternStringHandle("type"));
			inherited_type != nullptr && inherited_type != owner_info) {
			StringHandle inherited_member_name = StringTable::getOrInternStringHandle(
				StringBuilder()
					.append(inherited_type->name())
					.append("::")
					.append(member_name)
					.commit());
			if (auto folded = tryFoldDependentQualifiedStaticMemberNTTP(
					inherited_member_name,
					template_params,
					template_args);
				folded.has_value()) {
				return folded;
			}
		}
		return std::nullopt;
	};
	const StructTypeInfo* owner_struct = owner_info->getStructInfo();
	if (owner_struct == nullptr) {
		return tryFoldInheritedAliasMember();
	}
	StringHandle member_handle = StringTable::getOrInternStringHandle(member_name);
	auto [found_member, found_owner] =
		owner_struct->findStaticMemberRecursive(member_handle);
	if (found_member == nullptr || !found_member->initializer.has_value()) {
		return tryFoldInheritedAliasMember();
	}

	ConstExpr::EvaluationContext eval_ctx(gSymbolTable, *this);
	eval_ctx.struct_info = found_owner != nullptr ? found_owner : owner_struct;
	eval_ctx.storage_duration = ConstExpr::StorageDuration::Static;
	eval_ctx.template_environment = buildTemplateEnvironment(
		template_params,
		template_args,
		nullptr);
	ConstExpr::EvalResult eval_result =
		ConstExpr::Evaluator::evaluate(*found_member->initializer, eval_ctx);
	if (!eval_result.success()) {
		return std::nullopt;
	}
	return templateTypeArgFromEvalResult(eval_result);
}

const TypeInfo* Parser::tryMaterializeMemberAliasTemplateSpecialization(
	const TypeSpecifierNode& alias_type_spec,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args,
	const StructTypeInfo* instantiated_struct_info) {
	const TypeInfo* alias_template_pattern_info =
		tryGetTypeInfo(alias_type_spec.type_index());
	if (alias_template_pattern_info == nullptr) {
		return nullptr;
	}

	// Prefer the typedef's surface spelling (ConditionalT) over the deferred
	// helper specialization it currently points at (Conditional$...::type).
	std::string_view alias_template_name = alias_type_spec.token().value();
	if (alias_template_name.empty() ||
		!gTemplateRegistry.lookup_alias_template(alias_template_name).has_value()) {
		alias_template_name = {};
		StringHandle base_name = alias_template_pattern_info->baseTemplateName();
		if (base_name.isValid() &&
			gTemplateRegistry.lookup_alias_template(
				StringTable::getStringView(base_name)).has_value()) {
			alias_template_name = StringTable::getStringView(base_name);
		}
	}
	if (alias_template_name.empty() &&
		alias_template_pattern_info->sourceNamespace().isValid()) {
		StringHandle qualified_base =
			gNamespaceRegistry.buildQualifiedIdentifier(
				alias_template_pattern_info->sourceNamespace(),
				alias_template_pattern_info->baseTemplateName());
		if (qualified_base.isValid() &&
			gTemplateRegistry.lookup_alias_template(
				StringTable::getStringView(qualified_base)).has_value()) {
			alias_template_name = StringTable::getStringView(qualified_base);
		}
	}

	const TypeInfo* args_source_info = alias_template_pattern_info;
	if (args_source_info->isDependentMemberType() &&
		args_source_info->hasDependentQualifiedName()) {
		const TypeInfo::DependentQualifiedNameRecord* dependent_name =
			args_source_info->dependentQualifiedName();
		if (dependent_name != nullptr) {
			const TypeInfo* owner_info = nullptr;
			if (dependent_name->owner_type.is_valid()) {
				owner_info = tryGetTypeInfo(dependent_name->owner_type);
			}
			if (owner_info == nullptr && dependent_name->owner_name.isValid()) {
				owner_info = findTypeByName(dependent_name->owner_name);
			}
			if (owner_info != nullptr &&
				owner_info->isTemplateInstantiation() &&
				!owner_info->templateArgs().empty()) {
				args_source_info = owner_info;
			}
		}
	}
	if (!args_source_info->isTemplateInstantiation() ||
		args_source_info->templateArgs().empty()) {
		return nullptr;
	}

	// Expand parameter packs (e.g. `using Base = Recursive<Tail...>`) the same way
	// partial-spec / base-class materialization does. Bare materializeTemplateArgs
	// leaves Rest... dependent and can rematerialize the wrong Recursive arity.
	std::vector<TemplateTypeArg> concrete_alias_args =
		materializeTemplateArgsExpandingPacks(
			*args_source_info,
			template_params,
			template_args,
		[this](
			StringHandle dependent_name,
			const ASTNode& expr,
			std::span<const ASTNode> params,
			std::span<const TemplateTypeArg> args) -> std::optional<TemplateTypeArg> {
			if (dependent_name.isValid()) {
				InlineVector<TemplateParameterNode, 4> typed_params =
					collectTemplateParameterNodes(params);
				if (auto folded = tryFoldDependentQualifiedStaticMemberNTTP(
						dependent_name,
						std::span<const TemplateParameterNode>(
							typed_params.data(), typed_params.size()),
						args);
					folded.has_value()) {
					return folded;
				}
			}
			return this->evaluateDependentNTTPExpression(expr, params, args);
		});
	bool has_unresolved_alias_arg = false;
	for (TemplateTypeArg& concrete_arg : concrete_alias_args) {
		if (concrete_arg.is_value && concrete_arg.is_dependent) {
			if (concrete_arg.dependent_name.isValid() &&
				instantiated_struct_info != nullptr) {
				const StructStaticMember* referenced_static_member =
					instantiated_struct_info->findStaticMember(
						concrete_arg.dependent_name);
				if (referenced_static_member != nullptr &&
					referenced_static_member->initializer.has_value()) {
					ConstExpr::EvaluationContext eval_ctx(gSymbolTable, *this);
					eval_ctx.struct_info = instantiated_struct_info;
					eval_ctx.storage_duration = ConstExpr::StorageDuration::Static;
					auto eval_result = ConstExpr::Evaluator::evaluate(
						*referenced_static_member->initializer,
						eval_ctx);
					if (eval_result.success()) {
						concrete_arg.value = eval_result.as_int();
						concrete_arg.is_dependent = false;
						concrete_arg.dependent_name = StringHandle{};
						concrete_arg.dependent_expr.reset();
					}
				}
			}
			// Qualified static-member NTTP markers such as
			// `IsSimpleAlloc$...::value` are not members of the enclosing class.
			if (concrete_arg.is_dependent &&
				concrete_arg.dependent_name.isValid()) {
				std::string_view dependent_name_view =
					StringTable::getStringView(concrete_arg.dependent_name);
				if (dependent_name_view.find("::") != std::string_view::npos) {
					if (std::optional<TemplateTypeArg> folded =
							tryFoldDependentQualifiedStaticMemberNTTP(
								concrete_arg.dependent_name,
								template_params,
								template_args);
						folded.has_value()) {
						concrete_arg = *folded;
						concrete_arg.is_dependent = false;
						concrete_arg.dependent_name = StringHandle{};
						concrete_arg.dependent_expr.reset();
					}
				}
			}
			if (concrete_arg.is_dependent &&
				concrete_arg.dependent_expr.has_value()) {
				if (std::optional<TemplateTypeArg> folded =
						evaluateDependentNTTPExpression(
							*concrete_arg.dependent_expr,
							template_params,
							template_args);
					folded.has_value()) {
					concrete_arg = *folded;
					concrete_arg.is_dependent = false;
					concrete_arg.dependent_name = StringHandle{};
					concrete_arg.dependent_expr.reset();
				}
			}
		}
		if (concrete_arg.is_dependent ||
			concrete_arg.dependent_name.isValid() ||
			concrete_arg.dependent_expr.has_value()) {
			has_unresolved_alias_arg = true;
		}
	}
	if (has_unresolved_alias_arg) {
		return nullptr;
	}

	if (!alias_template_name.empty()) {
		AliasTemplateMaterializationResult materialized_alias_target =
			materializeAliasTemplateInstantiation(
				alias_template_name,
				concrete_alias_args);
		if (materialized_alias_target.resolved_type_info == nullptr &&
			!materialized_alias_target.instantiated_name.empty()) {
			materialized_alias_target.resolved_type_info =
				findTypeByName(StringTable::getOrInternStringHandle(
					materialized_alias_target.instantiated_name));
		}
		if (materialized_alias_target.resolved_type_info != nullptr) {
			return materialized_alias_target.resolved_type_info;
		}
	}

	// Deferred aliases like ConditionalT often rewrite the typedef token to the
	// helper class name (Conditional). Materialize that class with the folded
	// call-site arguments and then select the dependent member (usually ::type).
	StringHandle class_template_name = args_source_info->baseTemplateName();
	if (args_source_info->sourceNamespace().isValid() && class_template_name.isValid()) {
		class_template_name = gNamespaceRegistry.buildQualifiedIdentifier(
			args_source_info->sourceNamespace(),
			class_template_name);
	}
	std::string_view class_template_name_view =
		StringTable::getStringView(class_template_name);
	if (class_template_name_view.empty()) {
		return nullptr;
	}
	AliasTemplateMaterializationResult materialized_class =
		materializeTemplateInstantiationForLookup(
			class_template_name_view,
			concrete_alias_args);
	if (materialized_class.resolved_type_info == nullptr &&
		!materialized_class.instantiated_name.empty()) {
		materialized_class.resolved_type_info =
			findTypeByName(StringTable::getOrInternStringHandle(
				materialized_class.instantiated_name));
	}
	if (materialized_class.resolved_type_info == nullptr) {
		return nullptr;
	}
	if (alias_template_pattern_info->isDependentMemberType() &&
		alias_template_pattern_info->hasDependentQualifiedName()) {
		const TypeInfo::DependentQualifiedNameRecord* dependent_name =
			alias_template_pattern_info->dependentQualifiedName();
		if (dependent_name != nullptr && !dependent_name->member_chain.empty()) {
			StringHandle member_handle = dependent_name->member_chain.back().name;
			StringHandle qualified_member = StringTable::getOrInternStringHandle(
				StringBuilder()
					.append(materialized_class.instantiated_name)
					.append("::")
					.append(StringTable::getStringView(member_handle))
					.commit());
			if (const TypeInfo* member_info = findTypeByName(qualified_member);
				member_info != nullptr) {
				ResolvedAliasTypeInfo resolved_member =
					resolveAliasTypeInfo(
						member_info->registeredTypeIndex().withCategory(
							member_info->typeEnum()));
				if (resolved_member.terminal_type_info != nullptr) {
					return resolved_member.terminal_type_info;
				}
				return member_info;
			}
		}
	}
	return materialized_class.resolved_type_info;
}

std::optional<TypeSpecifierNode> Parser::rewriteDependentMemberTypeSpellings(
	const TypeSpecifierNode& dependent_type_spec,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args,
	const Token& result_token,
	CVQualifier cv_qualifier) {
	const TypeInfo* type_info = tryGetTypeInfo(dependent_type_spec.type_index());
	if (type_info == nullptr || !type_info->isDependentMemberType()) {
		return std::nullopt;
	}
	const TypeInfo::DependentQualifiedNameRecord* record = type_info->dependentQualifiedName();
	if (record == nullptr ||
		(record->owner_template_arguments.empty() && record->member_chain.empty())) {
		return std::nullopt;
	}

	TemplateEnvironment substitution_environment = buildTemplateEnvironment(
		template_params,
		template_args,
		nullptr);
	auto rewrite_arg_infos =
		[&](std::span<const TypeInfo::TemplateArgInfo> stored_args,
			bool& any_rebound) -> InlineVector<TypeInfo::TemplateArgInfo, 4> {
		InlineVector<TypeInfo::TemplateArgInfo, 4> rewritten_args;
		rewritten_args.reserve(stored_args.size());
		for (const TypeInfo::TemplateArgInfo& stored_arg : stored_args) {
			TemplateTypeArg arg = toTemplateTypeArg(stored_arg);
			if (arg.dependent_name.isValid()) {
				std::optional<TemplateTypeArg> bound_arg;
				if (const TemplateTypeArg* direct_bound_arg =
						substitution_environment.findOne(arg.dependent_name);
					direct_bound_arg != nullptr) {
					bound_arg = *direct_bound_arg;
				} else {
					bound_arg = resolveContextBinding(
						arg.dependent_name,
						substitution_environment);
				}
				if (bound_arg.has_value() && !bound_arg->is_value) {
					arg = rebindDependentTemplateTypeArg(*bound_arg, stored_arg);
					any_rebound = true;
				}
			}
			rewritten_args.push_back(toTemplateArgInfo(arg));
		}
		return rewritten_args;
	};

	bool any_rebound = false;
	TypeInfo::DependentQualifiedNameRecord rewritten_record = *record;
	rewritten_record.owner_template_arguments = rewrite_arg_infos(
		std::span<const TypeInfo::TemplateArgInfo>(
			record->owner_template_arguments.data(),
			record->owner_template_arguments.size()),
		any_rebound);
	for (auto& member_record : rewritten_record.member_chain) {
		if (!member_record.has_template_arguments) {
			continue;
		}
		bool member_rebound = false;
		member_record.template_arguments = rewrite_arg_infos(
			std::span<const TypeInfo::TemplateArgInfo>(
				member_record.template_arguments.data(),
				member_record.template_arguments.size()),
			member_rebound);
		any_rebound = any_rebound || member_rebound;
	}
	if (!any_rebound) {
		return std::nullopt;
	}

	std::vector<TemplateTypeArg> rewritten_args_for_naming;
	rewritten_args_for_naming.reserve(rewritten_record.owner_template_arguments.size());
	for (const TypeInfo::TemplateArgInfo& arg_info : rewritten_record.owner_template_arguments) {
		rewritten_args_for_naming.push_back(toTemplateTypeArg(arg_info));
	}
	std::string_view new_owner_name = get_instantiated_class_name(
		StringTable::getStringView(record->owner_name),
		std::span<const TemplateTypeArg>(
			rewritten_args_for_naming.data(),
			rewritten_args_for_naming.size()));
	if (new_owner_name.empty()) {
		return std::nullopt;
	}

	StringBuilder new_name_builder;
	new_name_builder.append(new_owner_name);
	for (const auto& member_record : rewritten_record.member_chain) {
		new_name_builder.append("::").append(
			StringTable::getStringView(member_record.name));
		if (member_record.has_template_arguments) {
			new_name_builder.append("<")
				.append(member_record.template_arguments.size())
				.append(" args>");
		}
	}
	StringHandle new_handle = StringTable::getOrInternStringHandle(new_name_builder.commit());

	const TypeInfo* new_info = nullptr;
	if (auto existing_it = getTypesByNameMap().find(new_handle);
		existing_it != getTypesByNameMap().end() && existing_it->second != nullptr) {
		new_info = existing_it->second;
	} else {
		TypeInfo& placeholder_type = add_empty_type_entry();
		placeholder_type.fallback_size_bits_ = 0;
		placeholder_type.name_ = new_handle;
		placeholder_type.is_incomplete_instantiation_ = true;
		placeholder_type.placeholder_kind_ = DependentPlaceholderKind::DependentMemberType;
		placeholder_type.setDependentQualifiedName(std::move(rewritten_record));
		if (const TypeInfo::InstantiationContext* source_context = type_info->instantiationContext();
			source_context != nullptr) {
			placeholder_type.setInstantiationContext(
				source_context->param_names,
				InlineVector<TypeInfo::TemplateArgInfo, 4>(source_context->param_args()),
				source_context->parent);
		}
		getTypesByNameMap()[new_handle] = &placeholder_type;
		new_info = &placeholder_type;
	}

	return TypeSpecifierNode(
		new_info->registeredTypeIndex().withCategory(TypeCategory::UserDefined),
		0,
		result_token,
		cv_qualifier,
		ReferenceQualifier::None);
}

const TypeInfo* Parser::materializeInstantiatedMemberAliasTarget(
	const TypeSpecifierNode& alias_type_spec,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args) {
	const TypeInfo* original_alias_target_info = tryGetTypeInfo(alias_type_spec.type_index());
	if (!original_alias_target_info) {
		return nullptr;
	}

	const TypeInfo* semantic_alias_target_info = original_alias_target_info;
	if ((!semantic_alias_target_info->isDependentMemberType() ||
		 !semantic_alias_target_info->hasDependentQualifiedName()) &&
		alias_type_spec.type_index().is_valid()) {
		ResolvedAliasTypeInfo resolved_alias =
			resolveAliasTypeInfo(alias_type_spec.type_index());
		if (resolved_alias.terminal_type_info != nullptr) {
			semantic_alias_target_info = resolved_alias.terminal_type_info;
		}
	}

	if (semantic_alias_target_info->isDependentMemberType() &&
		semantic_alias_target_info->hasDependentQualifiedName()) {
		if (const TypeInfo* resolved_dependent_type =
				resolveDependentMemberTypeSemantic(
					*semantic_alias_target_info,
					template_params,
					template_args,
					StringHandle{});
			resolved_dependent_type != nullptr) {
			return resolved_dependent_type;
		}
	}

	const TypeInfo::DependentQualifiedNameRecord* dependent_name =
		semantic_alias_target_info->dependentQualifiedName();
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
	if ((dependent_base_info == nullptr ||
		 !is_struct_type(dependent_base_info->typeEnum())) &&
		dependent_name->owner_name.isValid()) {
		for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
			if (template_params[i].nameHandle() == dependent_name->owner_name &&
				!template_args[i].is_value) {
				dependent_base_info = tryGetTypeInfo(template_args[i].type_index);
				break;
			}
		}
	}
	if (dependent_base_info == nullptr ||
		!is_struct_type(dependent_base_info->typeEnum())) {
		dependent_base_info =
			findUniqueStructOwnerTypeFromTemplateArgs(template_args);
	}
	if (!dependent_base_info || !is_struct_type(dependent_base_info->typeEnum())) {
		return nullptr;
	}
	if (semantic_alias_target_info->isDependentMemberType() &&
		semantic_alias_target_info->hasDependentQualifiedName()) {
		if (const TypeInfo* resolved_with_concrete_owner =
				resolveDependentMemberTypeSemantic(
					*semantic_alias_target_info,
					template_params,
					template_args,
					dependent_base_info->name());
			resolved_with_concrete_owner != nullptr) {
			return resolved_with_concrete_owner;
		}
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
		dependent_base_context != nullptr) {
		inherited_environment = buildTemplateEnvironment(*dependent_base_context);
		inherited_environment_ptr = &inherited_environment;
	}
	TemplateEnvironment alias_target_environment;
	if (const TypeInfo::InstantiationContext* alias_target_context =
			semantic_alias_target_info->instantiationContext();
		alias_target_context != nullptr) {
		alias_target_environment = buildTemplateEnvironment(*alias_target_context);
		alias_target_environment.parent = inherited_environment_ptr;
		inherited_environment_ptr = &alias_target_environment;
	}
	TemplateEnvironment substitution_environment = buildTemplateEnvironment(
		template_params,
		template_args,
		inherited_environment_ptr);
	auto materialize_template_args_with_environment =
		[&](
			std::span<const TypeInfo::TemplateArgInfo> stored_args) -> TemplateArgumentVector {
			TemplateArgumentVector concrete_args;
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
					[this, substitution_environment, owner_name = dependent_base_info->name()](
						const ASTNode& expr,
						std::span<const ASTNode> params,
						std::span<const TemplateTypeArg> args) {
						FlashCpp::ScopedStateCopy guard_subs(template_param_substitutions_);
						populateTemplateParamSubstitutions(template_param_substitutions_, substitution_environment);
						InlineVector<TemplateParameterNode, 4> typed_params =
							collectTemplateParameterNodes(params);
						return this->evaluateDependentNTTPExpression(
							expr,
							std::span<const TemplateParameterNode>(
								typed_params.data(),
								typed_params.size()),
							args,
							owner_name);
					});
			}
			concrete_args.push_back(std::move(concrete_arg));
		}
		return concrete_args;
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
		ResolvedAliasTypeInfo resolved_direct_concrete_member = resolveAliasTypeInfo(
			direct_concrete_member_it->second->registeredTypeIndex().withCategory(
				direct_concrete_member_it->second->typeEnum()));
		if (resolved_direct_concrete_member.terminal_type_info != nullptr &&
			isConcreteAliasSemanticSource(resolved_direct_concrete_member.terminal_type_info)) {
			if (typeAliasPreservesSurfaceModifiers(*direct_concrete_member_it->second)) {
				return direct_concrete_member_it->second;
			}
			return resolved_direct_concrete_member.terminal_type_info;
		}
		if (isConcreteAliasSemanticSource(direct_concrete_member_it->second)) {
			return direct_concrete_member_it->second;
		}
	}

	bool owner_chain_uses_member_templates = false;
	for (const auto& member_record : dependent_name->member_chain) {
		if (member_record.has_template_arguments) {
			owner_chain_uses_member_templates = true;
			break;
		}
	}

	std::string_view materialized_alias_base_name = dependent_base_name;
	if (dependent_base_info->isTemplateInstantiation() ||
		(!dependent_name->owner_template_arguments.empty() &&
		 !owner_chain_uses_member_templates)) {
		StringHandle base_template_name_handle = gNamespaceRegistry.buildQualifiedIdentifier(
			dependent_base_info->sourceNamespace(),
			dependent_base_info->baseTemplateName());
		auto resolve_composite_owner_template_name =
			[&](StringHandle candidate_handle) -> StringHandle {
			if (!candidate_handle.isValid()) {
				return candidate_handle;
			}
			std::string_view candidate_name =
				StringTable::getStringView(candidate_handle);
			size_t owner_sep = candidate_name.rfind("::");
			if (owner_sep == std::string_view::npos) {
				return candidate_handle;
			}
			std::string_view owner_prefix = candidate_name.substr(0, owner_sep);
			std::string_view member_template_name =
				candidate_name.substr(owner_sep + 2);
			for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
				const TemplateParameterNode* template_param =
					tryGetTemplateParameterNode(template_params[i]);
				if (template_param == nullptr ||
					template_param->name() != owner_prefix ||
					template_args[i].is_value) {
					continue;
				}
				const TypeInfo* concrete_owner_type_info =
					tryGetTypeInfo(template_args[i].type_index);
				if (concrete_owner_type_info == nullptr) {
					break;
				}
				return buildQualifiedName(
					StringTable::getStringView(concrete_owner_type_info->name()),
					member_template_name);
			}
			return candidate_handle;
		};
		if (dependent_name->owner_name.isValid()) {
			base_template_name_handle =
				resolve_composite_owner_template_name(
					dependent_name->owner_name);
		}
		if (!base_template_name_handle.isValid() &&
			dependent_name->owner_name.isValid()) {
			base_template_name_handle = dependent_name->owner_name;
		}
		TemplateArgumentVector concrete_base_args =
			!dependent_name->owner_template_arguments.empty()
				? materialize_template_args_with_environment(
					  dependent_name->owner_template_arguments)
				: materialize_template_args_with_environment(dependent_base_info->templateArgs());
		if (template_args_still_dependent(concrete_base_args)) {
			return nullptr;
		}
		std::string_view owner_lookup_name =
			base_template_name_handle.isValid()
				? StringTable::getStringView(base_template_name_handle)
				: dependent_base_name;
		AliasTemplateMaterializationResult materialized_alias_base =
			resolveCanonicalInstantiatedOwnerForLookup(
				owner_lookup_name,
				concrete_base_args);
		if (materialized_alias_base.instantiated_name.empty() &&
			materialized_alias_base.resolved_type_info == nullptr) {
			materialized_alias_base =
				materializeTemplateInstantiationForLookup(
					owner_lookup_name,
					concrete_base_args);
		}
		if (materialized_alias_base.instantiated_name.empty() &&
			base_template_name_handle != dependent_base_info->baseTemplateName()) {
			materialized_alias_base = materializeTemplateInstantiationForLookup(
				StringTable::getStringView(dependent_base_info->baseTemplateName()),
				concrete_base_args);
		}
		materialized_alias_base_name = materialized_alias_base.canonicalName();
		if (materialized_alias_base_name.empty()) {
			return nullptr;
		}
	}

	bool has_member_template_segment = false;
	for (const auto& member_record : dependent_name->member_chain) {
		if (member_record.has_template_arguments) {
			has_member_template_segment = true;
			break;
		}
	}
	if (has_member_template_segment) {
		std::vector<QualifiedTypeMemberAccess> materialized_member_chain;
		materialized_member_chain.reserve(dependent_name->member_chain.size());
		bool defer_to_alias_materialization = false;
		for (const auto& member_record : dependent_name->member_chain) {
			QualifiedTypeMemberAccess member_access;
			member_access.member_name = member_record.name;
			if (member_record.has_template_arguments) {
				TemplateArgumentVector concrete_member_args =
					materialize_template_args_with_environment(
						member_record.template_arguments);
				if (template_args_still_dependent(concrete_member_args)) {
					const bool is_terminal_member =
						&member_record == &dependent_name->member_chain.back();
					if (!is_terminal_member ||
						original_alias_target_info->templateArgs().empty()) {
						defer_to_alias_materialization = true;
						break;
					}
					TemplateArgumentVector fallback_member_args =
						materialize_template_args_with_environment(
							original_alias_target_info->templateArgs());
					if (template_args_still_dependent(fallback_member_args) ||
						(!member_record.template_arguments.empty() &&
						 fallback_member_args.size() !=
							 member_record.template_arguments.size())) {
						defer_to_alias_materialization = true;
						break;
					}
					concrete_member_args = std::move(fallback_member_args);
				}
				std::vector<TemplateTypeArg> materialized_args(
					concrete_member_args.begin(),
					concrete_member_args.end());
				member_access.has_template_arguments = true;
				member_access.template_arguments =
					&gChunkedAnyStorage.emplace_back<std::vector<TemplateTypeArg>>(
						std::move(materialized_args));
			}
			materialized_member_chain.push_back(std::move(member_access));
		}

		if (!defer_to_alias_materialization) {
			if (const TypeInfo* resolved_member =
					resolveBaseClassMemberTypeChain(
						materialized_alias_base_name,
						std::span<const QualifiedTypeMemberAccess>(
							materialized_member_chain.data(),
							materialized_member_chain.size()));
				resolved_member != nullptr) {
				ResolvedAliasTypeInfo resolved_member_alias =
					resolveAliasTypeInfo(
						resolved_member->registeredTypeIndex().withCategory(
							resolved_member->typeEnum()));
				if (resolved_member_alias.terminal_type_info != nullptr &&
					!resolved_member_alias.terminal_type_info->isDependentMemberType()) {
					if (typeAliasPreservesSurfaceModifiers(*resolved_member)) {
						return resolved_member;
					}
					return resolved_member_alias.terminal_type_info;
				}
				if (!resolved_member->isDependentMemberType()) {
					return resolved_member;
				}
			}
		}
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
		if (dependent_name->owner_name.isValid()) {
			StringHandle pattern_member_alias_handle =
				StringTable::getOrInternStringHandle(
					StringBuilder()
						.append(StringTable::getStringView(dependent_name->owner_name))
						.append("::")
						.append(dependent_member_path)
						.commit());
			if (gTemplateRegistry.lookup_alias_template(pattern_member_alias_handle).has_value()) {
				materialized_member_alias_handle = pattern_member_alias_handle;
			}
		}
	}
	if (!gTemplateRegistry.lookup_alias_template(materialized_member_alias_handle).has_value()) {
		if (dependent_base_info->baseTemplateName().isValid()) {
			StringHandle pattern_member_alias_handle =
				StringTable::getOrInternStringHandle(
					StringBuilder()
						.append(StringTable::getStringView(dependent_base_info->baseTemplateName()))
						.append("::")
						.append(dependent_member_path)
						.commit());
			if (gTemplateRegistry.lookup_alias_template(pattern_member_alias_handle).has_value()) {
				materialized_member_alias_handle = pattern_member_alias_handle;
			}
		}
	}
	if (!gTemplateRegistry.lookup_alias_template(materialized_member_alias_handle).has_value()) {
		const size_t instantiated_marker = dependent_base_name.find('$');
		const size_t nested_owner_sep = dependent_base_name.find("::");
		if (instantiated_marker != std::string_view::npos &&
			nested_owner_sep != std::string_view::npos &&
			instantiated_marker < nested_owner_sep) {
			StringHandle pattern_member_alias_handle =
				StringTable::getOrInternStringHandle(
					StringBuilder()
						.append(dependent_base_name.substr(0, instantiated_marker))
						.append(dependent_base_name.substr(nested_owner_sep))
						.append("::")
						.append(dependent_member_path)
						.commit());
			if (gTemplateRegistry.lookup_alias_template(pattern_member_alias_handle).has_value()) {
				materialized_member_alias_handle = pattern_member_alias_handle;
			}
		}
	}
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
	auto member_alias_entry =
		gTemplateRegistry.lookup_alias_template(
			StringTable::getStringView(materialized_member_alias_handle));
	if (member_alias_entry.has_value()) {
		TemplateArgumentVector concrete_member_template_args;
		size_t member_alias_param_count = 0;
		if (member_alias_entry->is<TemplateAliasNode>()) {
			member_alias_param_count =
				member_alias_entry->as<TemplateAliasNode>().template_parameters().size();
		}
		if (dependent_member.has_template_arguments) {
			concrete_member_template_args =
				materialize_template_args_with_environment(
					dependent_member.template_arguments);
		} else if (member_alias_param_count != 0 &&
				   !original_alias_target_info->templateArgs().empty()) {
			concrete_member_template_args =
				materialize_template_args_with_environment(original_alias_target_info->templateArgs());
			if (concrete_member_template_args.size() > member_alias_param_count) {
				concrete_member_template_args.resize(member_alias_param_count);
			}
		}
		AliasTemplateMaterializationResult materialized_member_alias =
			materializeAliasTemplateInstantiation(
				StringTable::getStringView(materialized_member_alias_handle),
				concrete_member_template_args);
		if (materialized_member_alias.resolved_type_info != nullptr) {
			return materialized_member_alias.resolved_type_info;
		}
		if (member_alias_entry.has_value() &&
			member_alias_entry->is<TemplateAliasNode>()) {
			const TemplateAliasNode& member_alias_node =
				member_alias_entry->as<TemplateAliasNode>();
			std::vector<TemplateParameterNode> combined_template_params;
			std::vector<TemplateTypeArg> combined_template_args;
			combined_template_params.reserve(
				template_params.size() +
				member_alias_node.template_parameters().size());
			combined_template_args.reserve(
				template_args.size() +
				concrete_member_template_args.size());
			combined_template_params.insert(
				combined_template_params.end(),
				template_params.begin(),
				template_params.end());
			combined_template_args.insert(
				combined_template_args.end(),
				template_args.begin(),
				template_args.end());
			const auto& member_alias_template_params =
				member_alias_node.template_parameters();
			const size_t member_alias_arg_count = std::min(
				member_alias_template_params.size(),
				concrete_member_template_args.size());
			for (size_t i = 0; i < member_alias_arg_count; ++i) {
				combined_template_params.push_back(
					member_alias_template_params[i]);
				combined_template_args.push_back(
					concrete_member_template_args[i]);
			}
			ASTNode substituted_alias_target =
				substituteTemplateParameters(
					ASTNode(&member_alias_node.target_type_node()),
					combined_template_params,
					combined_template_args);
			if (substituted_alias_target.is<TypeSpecifierNode>()) {
				const TypeSpecifierNode& substituted_alias_target_spec =
					substituted_alias_target.as<TypeSpecifierNode>();
				if (const TypeInfo* substituted_alias_target_info =
						tryGetTypeInfo(substituted_alias_target_spec.type_index());
					substituted_alias_target_info != nullptr) {
					ResolvedAliasTypeInfo resolved_alias_target =
						resolveAliasTypeInfo(
							substituted_alias_target_info->registeredTypeIndex().withCategory(
								substituted_alias_target_info->typeEnum()));
					if (resolved_alias_target.terminal_type_info != nullptr) {
						return resolved_alias_target.terminal_type_info;
					}
					return substituted_alias_target_info;
				}
			}
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
		ResolvedAliasTypeInfo resolved_concrete_member = resolveAliasTypeInfo(
			concrete_member_it->second->registeredTypeIndex().withCategory(
				concrete_member_it->second->typeEnum()));
		if (resolved_concrete_member.terminal_type_info != nullptr &&
			isConcreteAliasSemanticSource(resolved_concrete_member.terminal_type_info)) {
			if (typeAliasPreservesSurfaceModifiers(*concrete_member_it->second)) {
				return concrete_member_it->second;
			}
			return resolved_concrete_member.terminal_type_info;
		}
		if (isConcreteAliasSemanticSource(concrete_member_it->second)) {
			return concrete_member_it->second;
		}
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

	TypeSpecifierNode resolved_outer_spec = type_spec;
	if (auto alias_entry = gTemplateRegistry.lookup_alias_template(alias_template_name);
		alias_entry.has_value() && alias_entry->is<TemplateAliasNode>()) {
		const TemplateAliasNode& alias_node = alias_entry->as<TemplateAliasNode>();
		const TypeSpecifierNode& alias_target_type_spec =
			alias_node.target_type_node();
		const bool alias_target_preserves_surface =
			alias_target_type_spec.cv_qualifier() != CVQualifier::None ||
			alias_target_type_spec.reference_qualifier() != ReferenceQualifier::None ||
			alias_target_type_spec.pointer_depth() != 0 ||
			alias_target_type_spec.has_function_signature() ||
			alias_target_type_spec.is_array();
		ASTNode substituted_alias_target_node = substituteTemplateParameters(
			ASTNode(&alias_target_type_spec),
			alias_node.template_parameters(),
			template_args);
		if (substituted_alias_target_node.is<TypeSpecifierNode>()) {
			const TypeSpecifierNode& substituted_alias_target_spec =
				substituted_alias_target_node.as<TypeSpecifierNode>();
			if (alias_target_preserves_surface) {
				if (const TypeInfo* concrete_member_alias =
						materializeInstantiatedMemberAliasTarget(
							substituted_alias_target_spec,
							alias_node.template_parameters(),
							template_args);
					concrete_member_alias != nullptr) {
					type_spec = resolveTypeInfoToTypeSpec(
						*concrete_member_alias,
						alias_target_type_spec);
					return true;
				}
				resolved_outer_spec = alias_target_type_spec;
			}
		}
	}

	type_spec = resolveTypeInfoToTypeSpec(
		*materialized_alias.resolved_type_info,
		resolved_outer_spec);
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

	TemplateArgumentVector concrete_args;
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
	if (auto current_instantiation =
			tryResolveCurrentInstantiationTemplateOwner(base_class_name, template_args);
		current_instantiation.has_value() &&
		!current_instantiation->instantiated_name.empty()) {
		base_class_name = current_instantiation->instantiated_name;
		return base_class_name;
	}

	// First check if the base class is a template alias (like bool_constant)
	auto alias_entry = gTemplateRegistry.lookup_alias_template(base_class_name);
	if (alias_entry.has_value()) {
		FLASH_LOG(Parser, Debug, "Base class '", base_class_name, "' is a template alias - resolving");

		const TemplateAliasNode& alias_node = alias_entry->as<TemplateAliasNode>();
		if (!alias_node.is_deferred()) {
			std::optional<TemplateTypeArg> rebound_alias_arg =
				tryRebindAliasTargetTemplateArg(alias_node, template_args);
			// If the alias's own params don't contain the target (e.g.
			// `template<typename U> using Apply = T` where T is an outer param),
			// fall back to the stored outer binding for this alias template name.
			if (!rebound_alias_arg.has_value()) {
				const OuterTemplateBinding* outer = gTemplateRegistry.getOuterTemplateBinding(base_class_name);
				if (outer != nullptr) {
					StringHandle alias_target_name =
						getAliasTargetNameHandle(alias_node.target_type_node());
					if (alias_target_name.isValid()) {
						for (size_t i = 0;
							 i < outer->param_names.size() && i < outer->param_args.size();
							 ++i) {
							if (outer->param_names[i] == alias_target_name) {
								rebound_alias_arg = outer->param_args[i];
								break;
							}
						}
					}
				}
			}
			if (rebound_alias_arg.has_value() && !rebound_alias_arg->is_value) {
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
			if (const TypeInfo* alias_target_info =
					tryGetTypeInfo(alias_target_type.type_index());
				alias_target_info != nullptr &&
				alias_target_info->deferredDecltypeExpression() != nullptr) {
				const OuterTemplateBinding* outer =
					gTemplateRegistry.getOuterTemplateBinding(base_class_name);
				std::optional<TemplateEnvironment> outer_environment;
				if (outer != nullptr) {
					outer_environment = buildTemplateEnvironment(*outer);
				}
				TemplateInstantiationContext substitution_context =
					buildTemplateInstantiationContext(
						alias_node.template_parameters(),
						template_args,
						outer_environment.has_value() ? &*outer_environment : nullptr,
						currentTemplateSubstitutionFailurePolicy());
				ExpressionSubstitutor substitutor(substitution_context, *this);
				ASTNode substituted_expr =
					substitutor.substitute(*alias_target_info->deferredDecltypeExpression());
				if (auto resolved_type = get_expression_type(substituted_expr);
					resolved_type.has_value()) {
					if (const TypeInfo* resolved_type_info =
							tryGetTypeInfo(resolved_type->type_index());
						resolved_type_info != nullptr) {
						base_class_name =
							StringTable::getStringView(resolved_type_info->name());
						return base_class_name;
					}
					std::string_view builtin_name =
						getTypeName(resolved_type->type());
					if (!builtin_name.empty()) {
						base_class_name = builtin_name;
						return builtin_name;
					}
				}
			}
			if (const TypeInfo* alias_target_info = tryGetTypeInfo(alias_target_type.type_index());
				alias_target_info != nullptr && alias_target_info->isTemplateInstantiation()) {
				std::vector<TemplateTypeArg> concrete_target_args =
					materializeTemplateArgs(
						*alias_target_info,
						alias_node.template_parameters(),
						template_args,
					[this, owner_name = alias_target_info->name()](
						StringHandle dependent_name,
						const ASTNode& expr,
						std::span<const ASTNode> params,
						std::span<const TemplateTypeArg> args) -> std::optional<TemplateTypeArg> {
						InlineVector<TemplateParameterNode, 4> typed_params =
							collectTemplateParameterNodes(params);
						if (dependent_name.isValid()) {
							if (auto folded = tryFoldDependentQualifiedStaticMemberNTTP(
									dependent_name,
									std::span<const TemplateParameterNode>(
										typed_params.data(), typed_params.size()),
									args);
								folded.has_value()) {
								return folded;
							}
						}
						return this->evaluateDependentNTTPExpression(
								expr,
								std::span<const TemplateParameterNode>(
									typed_params.data(),
									typed_params.size()),
								args,
								owner_name);
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
			auto substituted_args_opt = materializeDeferredAliasTemplateArgs(
				alias_node,
				template_args,
				gTemplateRegistry.getOuterTemplateBinding(base_class_name));
			if (!substituted_args_opt.has_value()) {
				return std::string_view();
			}
			TemplateArgumentVector substituted_args = std::move(*substituted_args_opt);

			// Now recursively instantiate the target template
			// The target might itself be a template alias (chain of aliases)
			std::string_view target_name(alias_node.target_template_name());
			std::string_view instantiated_name;
			if (!target_name.empty() &&
				target_name != base_class_name &&
				gTemplateRegistry.lookup_alias_template(target_name).has_value()) {
				AliasTemplateMaterializationResult materialized_target_alias =
					materializeAliasTemplateInstantiation(target_name, substituted_args);
				if (materialized_target_alias.resolved_type_info != nullptr) {
					instantiated_name =
						StringTable::getStringView(materialized_target_alias.resolved_type_info->name());
				} else {
					instantiated_name = materialized_target_alias.instantiated_name;
				}
			}
			if (instantiated_name.empty()) {
				instantiated_name = instantiate_and_register_base_template(target_name, substituted_args);
			}
			if (!instantiated_name.empty()) {
				if (alias_node.hasDeferredMemberTarget()) {
					if (!hasComplexDeferredMemberChain(alias_node)) {
						base_class_name = instantiated_name;
						return instantiated_name;
					}
					if (const TypeInfo* materialized_member =
							materializeDeferredAliasMemberTemplateChain(
								alias_node,
								template_args,
								instantiated_name);
						materialized_member != nullptr) {
						base_class_name = StringTable::getStringView(materialized_member->name());
						return base_class_name;
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
					TemplateArgumentVector filled_args_inline =
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
					FLASH_LOG(Templates, Trace, "Filled in default type argument for param ", i);
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
						FLASH_LOG(Templates, Trace, "Filled in default non-type argument for param ", i);
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
	FLASH_LOG(Templates, Trace, "sizeof substitution: resolved member alias ", StringTable::getStringView(qualified_alias_name),
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
	auto makeSubstitutedTypeNode =
		[&](const TemplateTypeArg& arg, const Token& token) {
		// Pointers are always 64 bits on x64 regardless of the pointee type.
		// Using get_type_size_bits(arg.category()) alone would give the base
		// type size (e.g. 32 for int) instead of the pointer size (64) when
		// the template argument carries pointer modifiers (e.g. T* where T=int).
		int size_bits = arg.pointer_depth > 0
			? 64
			: get_type_size_bits(arg.category());
		TypeSpecifierNode new_type(
			arg.typeEnum(),
			TypeQualifier::None,
			size_bits,
			token,
			arg.cv_qualifier);
		new_type.set_type_index(arg.type_index);
		new_type.set_reference_qualifier(arg.ref_qualifier);
		for (size_t p = 0; p < arg.pointer_depth; ++p) {
			CVQualifier pointer_cv = CVQualifier::None;
			if (p < arg.pointer_cv_qualifiers.size()) {
				pointer_cv = arg.pointer_cv_qualifiers[p];
			}
			new_type.add_pointer_level(pointer_cv);
		}
		if (arg.is_array && !arg.array_dimensions.empty()) {
			new_type.set_array_dimensions(arg.array_dimensions);
		}
		if (arg.function_signature.has_value()) {
			new_type.set_function_signature(*arg.function_signature);
		}
		return new_type;
	};

	// ASTNode is a typed pointer wrapper, check if it contains an ExpressionNode
	if (!expr.is<ExpressionNode>()) {
		FLASH_LOG(Templates, Trace, "substitute_template_params_in_expression: not an ExpressionNode");
		return expr; // Return as-is if not an expression
	}

	const ExpressionNode& expr_variant = expr.as<ExpressionNode>();
	FLASH_LOG(Templates, Trace, "substitute_template_params_in_expression: processing expression, variant index=", expr_variant.index());

	// Handle sizeof expressions
	if (std::holds_alternative<SizeofExprNode>(expr_variant)) {
		const SizeofExprNode& sizeof_node = std::get<SizeofExprNode>(expr_variant);

		// If sizeof has a type operand, check if it needs substitution
		if (sizeof_node.is_type() && sizeof_node.type_or_expr().is<TypeSpecifierNode>()) {
			const TypeSpecifierNode& type_node = sizeof_node.type_or_expr().as<TypeSpecifierNode>();

			FLASH_LOG(Templates, Trace, "sizeof substitution: checking type_index=", type_node.type_index(),
					  " type=", static_cast<int>(type_node.type()));

			// First, try to find by type_index
			auto it = type_substitution_map.find(type_node.type_index());
			if (it != type_substitution_map.end()) {
				FLASH_LOG(Templates, Trace, "sizeof substitution: FOUND match by type_index, substituting with ", it->second.toString());

				// Create a new type node with the substituted type
				const TemplateTypeArg& arg = it->second;
				TypeSpecifierNode new_type =
					makeSubstitutedTypeNode(arg, sizeof_node.sizeof_token());

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
					FLASH_LOG(Templates, Trace, "sizeof substitution: checking by name: ", type_name);

					// Search substitution map for any entry where the key type_index has the same name
					for (const auto& [key_type_index, arg] : type_substitution_map) {
						if (const TypeInfo* key_type_info = tryGetTypeInfo(key_type_index)) {
							std::string_view param_name = StringTable::getStringView(key_type_info->name());
							if (param_name == type_name) {
								FLASH_LOG(Templates, Trace, "sizeof substitution: FOUND match by name, substituting with ", arg.toString());

							// Create a new type node with the substituted type
								TypeSpecifierNode new_type =
									makeSubstitutedTypeNode(arg, sizeof_node.sizeof_token());

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
						TypeSpecifierNode new_type =
							makeSubstitutedTypeNode(arg, sizeof_node.sizeof_token());
						auto new_type_node = emplace_node<TypeSpecifierNode>(new_type);
						SizeofExprNode new_sizeof(new_type_node, sizeof_node.sizeof_token());
						return emplace_node<ExpressionNode>(new_sizeof);
					}
				}

				if (substitution_owner.isValid()) {
					const TypeInfo* owner_type_info = findTypeByName(substitution_owner);
					OuterTemplateBinding owner_binding = buildAccumulatedOuterTemplateBinding(
						owner_type_info,
						nullptr,
						substitution_owner);
					for (size_t binding_index = 0;
						 binding_index < owner_binding.param_names.size() &&
						 binding_index < owner_binding.param_args.size();
						 ++binding_index) {
						if (StringTable::getStringView(owner_binding.param_names[binding_index]) != type_name) {
							continue;
						}
						const TemplateTypeArg& arg = owner_binding.param_args[binding_index];
						if (arg.is_value || !arg.type_index.is_valid()) {
							break;
						}
						TypeSpecifierNode new_type =
							makeSubstitutedTypeNode(arg, sizeof_node.sizeof_token());
						auto new_type_node = emplace_node<TypeSpecifierNode>(new_type);
						SizeofExprNode new_sizeof(new_type_node, sizeof_node.sizeof_token());
						return emplace_node<ExpressionNode>(new_sizeof);
					}
				}
			}

			if (auto resolved = tryResolveSizeofMemberAlias(substitution_owner, type_node.token().value(), sizeof_node.sizeof_token())) {
				return *resolved;
			}

			FLASH_LOG(Templates, Trace, "sizeof substitution: NO match found");
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
		const auto tryFindTypeSubstitution = [&]() -> const TemplateTypeArg* {
			if (auto it = type_substitution_map.find(ctor_type.type_index());
				it != type_substitution_map.end()) {
				return &it->second;
			}
			if (ctor_type.category() == TypeCategory::UserDefined ||
				ctor_type.category() == TypeCategory::TypeAlias ||
				ctor_type.category() == TypeCategory::Template) {
				std::string_view ctor_type_name = ctor_type.token().value();
				if (ctor_type_name.empty()) {
					if (const TypeInfo* ctor_type_info = tryGetTypeInfo(ctor_type.type_index())) {
						ctor_type_name = StringTable::getStringView(ctor_type_info->name());
					}
				}
				if (!ctor_type_name.empty()) {
					for (const auto& [key_type_index, arg] : type_substitution_map) {
						if (const TypeInfo* key_type_info = tryGetTypeInfo(key_type_index)) {
							if (StringTable::getStringView(key_type_info->name()) == ctor_type_name) {
								return &arg;
							}
						}
					}
				}
			}
			if ((ctor_type.category() == TypeCategory::UserDefined ||
				 ctor_type.category() == TypeCategory::TypeAlias ||
				 ctor_type.category() == TypeCategory::Template) &&
				type_substitution_map.size() == 1) {
				return &type_substitution_map.begin()->second;
			}
			return nullptr;
		};

		if (const TemplateTypeArg* substituted_ctor_arg = tryFindTypeSubstitution();
			substituted_ctor_arg != nullptr) {
			ChunkedVector<ASTNode> new_args;
			for (size_t i = 0; i < ctor.arguments().size(); ++i) {
				new_args.push_back(substitute_template_params_in_expression(
					ctor.arguments()[i],
					type_substitution_map,
					nontype_substitution_map,
					substitution_owner));
			}

			auto new_type_node = emplace_node<TypeSpecifierNode>(
				makeSubstitutedTypeNode(*substituted_ctor_arg, ctor.called_from()));
			ConstructorCallNode new_ctor(new_type_node, std::move(new_args), ctor.called_from());
			return emplace_node<ExpressionNode>(new_ctor);
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
		auto findTypeSubstitution = [&](const TypeSpecifierNode& type_node)
			-> std::unordered_map<TypeIndex, TemplateTypeArg>::const_iterator {
			auto it = type_substitution_map.find(type_node.type_index());
			if (it != type_substitution_map.end()) {
				return it;
			}
			if (type_node.category() != TypeCategory::UserDefined &&
				type_node.category() != TypeCategory::TypeAlias &&
				type_node.category() != TypeCategory::Template) {
				return type_substitution_map.end();
			}
			std::string_view type_name = type_node.token().value();
			if (type_name.empty()) {
				if (const TypeInfo* ti = tryGetTypeInfo(type_node.type_index())) {
					type_name = StringTable::getStringView(ti->name());
				}
			}
			for (const auto& [key_type_index, arg] : type_substitution_map) {
				if (const TypeInfo* ki = tryGetTypeInfo(key_type_index)) {
					if (StringTable::getStringView(ki->name()) == type_name) {
						return type_substitution_map.find(key_type_index);
					}
				}
			}
			return type_substitution_map.end();
		};
		auto substituteTraitTypeOperand = [&](const ASTNode& operand_node) -> ASTNode {
			if (!operand_node.is<TypeSpecifierNode>()) {
				return operand_node;
			}
			const TypeSpecifierNode& type_node = operand_node.as<TypeSpecifierNode>();
			auto it = findTypeSubstitution(type_node);
			if (it == type_substitution_map.end()) {
				return operand_node;
			}
			return emplace_node<TypeSpecifierNode>(
				makeSubstitutedTypeNode(it->second, type_node.token()));
		};
		if (trait_expr.has_type() && trait_expr.type_node().is<TypeSpecifierNode>()) {
			return emplace_node<ExpressionNode>(
				rebuildTypeTraitExpr(
					trait_expr,
					substituteTraitTypeOperand(trait_expr.type_node()),
					substituteTraitTypeOperand));
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

			FLASH_LOG(Templates, Trace, "sizeof substitution: checking type_index=", type_node.type_index(),
					  " type=", static_cast<int>(type_node.type()));

			// Check if this type needs substitution
			auto it = type_substitution_map.find(type_node.type_index());
			if (it != type_substitution_map.end()) {
				FLASH_LOG(Templates, Trace, "sizeof substitution: FOUND match, substituting with ", it->second.toString());

				// Create a new type node with the substituted type
				const TemplateTypeArg& arg = it->second;
				// Pointers are always 64 bits on x64 regardless of the pointee type.
				int size_bits = arg.pointer_depth > 0
					? 64
					: get_type_size_bits(arg.category());
				TypeSpecifierNode new_type(
					arg.typeEnum(),
					TypeQualifier::None,
					size_bits,
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
				FLASH_LOG(Templates, Trace, "sizeof substitution: NO match found in map");
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
std::optional<ASTNode> Parser::try_instantiate_variable_template(
	std::string_view template_name,
	std::span<const TemplateTypeArg> template_args,
	const OuterTemplateBinding* explicit_outer_binding) {
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
					FLASH_LOG(Templates, Trace, "Resolving dependent template parameter '", dep_name,
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
						FLASH_LOG(Templates, Trace, "Resolved type alias arg from ", arg.type_index, " to ", resolved.type_index);
						arg = resolved;
					}
				}
				else {
					StringHandle type_name = type_info->name();
					for (const auto& subst : template_param_substitutions_) {
						if (subst.is_type_param && subst.param_name == type_name && !subst.substituted_type.is_dependent) {
							FLASH_LOG(Templates, Trace, "Substituting template parameter '", type_name,
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
			FLASH_LOG(Templates, Trace, "Skipping variable template '", template_name,
					  "' instantiation - arg[", i, "] is dependent: ", arg.toString());
			return std::nullopt;
		}
	}

	auto template_opt = gTemplateRegistry.lookupVariableTemplate(template_name);
	if (!template_opt.has_value() && template_name != simple_template_name) {
		template_opt = gTemplateRegistry.lookupVariableTemplate(simple_template_name);
	}
	if (!template_opt.has_value()) {
		FLASH_LOG(Templates, Trace, "Variable template '", template_name, "' not found");
		return std::nullopt;
	}

	if (!template_opt->is<TemplateVariableDeclarationNode>()) {
		FLASH_LOG(Templates, Error, "Expected TemplateVariableDeclarationNode");
		return std::nullopt;
	}

	const TemplateVariableDeclarationNode& var_template = template_opt->as<TemplateVariableDeclarationNode>();
	const auto& template_params = var_template.template_parameters();
	const OuterTemplateBinding* outer_binding =
		explicit_outer_binding != nullptr
			? explicit_outer_binding
			: gTemplateRegistry.getOuterTemplateBinding(template_name);

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
			TemplateArgumentVector bound_args_inline = toInlineTemplateArgs(bound_args);
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
	std::vector<TemplateTypeArg> effective_instantiation_args;
	if (outer_binding != nullptr) {
		const auto& outer_args =
			!outer_binding->all_args.empty()
				? outer_binding->all_args
				: outer_binding->param_args;
		effective_instantiation_args.reserve(outer_args.size() + filled_args.size());
		for (const TemplateTypeArg& outer_arg : outer_args) {
			effective_instantiation_args.push_back(outer_arg);
		}
		for (const TemplateTypeArg& filled_arg : filled_args) {
			effective_instantiation_args.push_back(filled_arg);
		}
	}
	std::span<const TemplateTypeArg> instantiation_identity_args =
		effective_instantiation_args.empty()
			? filled_args
			: std::span<const TemplateTypeArg>(
				  effective_instantiation_args.data(),
				  effective_instantiation_args.size());
	std::vector<TemplateParameterNode> effective_template_params_storage;
	std::vector<TemplateTypeArg> effective_template_args_storage;
	std::span<const TemplateParameterNode> effective_template_params;
	std::span<const TemplateTypeArg> effective_template_args;
	buildEffectiveVariableTemplateSubstitutionInputs(
		template_name,
		outer_binding,
		template_params,
		filled_args,
		effective_template_params_storage,
		effective_template_args_storage,
		effective_template_params,
		effective_template_args);

	// Structural pattern matching: find the best matching partial specialization
	// Uses TemplatePattern::matches() which handles qualifier matching, multi-arg,
	// and proper template parameter deduction without string-based pattern keys.
	auto structural_match = gTemplateRegistry.findVariableTemplateSpecialization(simple_template_name, filled_args);
	// Also try qualified name if simple name didn't match
	if (!structural_match.has_value() && template_name != simple_template_name) {
		structural_match = gTemplateRegistry.findVariableTemplateSpecialization(template_name, filled_args);
	}

	// Generate unique name for the instantiation using hash-based naming
	// This ensures consistent naming with class template instantiations
	std::string_view persistent_name = FlashCpp::generateInstantiatedNameFromArgs(simple_template_name, instantiation_identity_args);

	// Check if already instantiated
	if (std::optional<ASTNode> existing_instantiation = gSymbolTable.lookup(persistent_name);
		existing_instantiation.has_value()) {
		return existing_instantiation;
	}

	if (structural_match.has_value() && structural_match->node.is<TemplateVariableDeclarationNode>()) {
		FLASH_LOG(Templates, Trace, "Found variable template partial specialization via structural match");
		const TemplateVariableDeclarationNode& spec_template = structural_match->node.as<TemplateVariableDeclarationNode>();
		const VariableDeclarationNode& spec_var_decl = spec_template.variable_decl_node();
		const Token& orig_token = spec_var_decl.declaration().identifier_token();

		const DeclarationNode& spec_decl = spec_var_decl.declaration();
		ASTNode spec_type = spec_decl.type_node();
		const auto& spec_params = spec_template.template_parameters();
		TemplateArgumentVector converted_args;
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
			// Structural substitution: the declaration-time initializer AST is
			// substituted once under the specialization's binding environment.
			if (!spec_params.empty()) {
				init_expr = substituteTemplateParameters(
					*spec_var_decl.initializer(), spec_params, converted_args);
			} else {
				init_expr = *spec_var_decl.initializer();
			}
			if (!spec_params.empty()) {
				spec_type = substituteTemplateParameters(
					spec_type, spec_params, converted_args);
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

	// Get the original variable declaration
	const VariableDeclarationNode& orig_var_decl = var_template.variable_decl_node();
	const DeclarationNode& orig_decl = orig_var_decl.declaration();
	TemplateArgumentVector filled_args_inline = toInlineTemplateArgs(filled_args);
	TemplateInstantiationContext variable_template_context =
		buildTemplateInstantiationContext(
			effective_template_params,
			effective_template_args,
			nullptr,
			currentTemplateSubstitutionFailurePolicy());
	ASTNode substituted_type = substituteTemplateParameters(orig_decl.type_node(), variable_template_context);

	// Create new declaration with substituted type and instantiated name
	// Use original token's line/column/file info for better diagnostics
	const Token& orig_token = orig_decl.identifier_token();
	Token instantiated_name_token(Token::Type::Identifier, persistent_name, orig_token.line(), orig_token.column(), orig_token.file_index());
	auto new_decl_node = emplace_node<DeclarationNode>(substituted_type, instantiated_name_token);

	// Substitute template parameters in initializer expression
	std::optional<ASTNode> new_initializer = std::nullopt;
	if (orig_var_decl.initializer().has_value()) {
		FLASH_LOG(Templates, Trace, "Substituting initializer expression for variable template");
		// Structural substitution: the declaration-time initializer AST is
		// substituted once under the instantiation's binding environment; no
		// lexer replay of source text is needed.
		new_initializer = substituteTemplateParameters(
			orig_var_decl.initializer().value(),
			variable_template_context);
		FLASH_LOG(Templates, Trace, "Initializer substitution complete");

		// PHASE 3 FIX: After substitution, trigger instantiation of any class templates
		// referenced in the initializer expression. This ensures specialization pattern
		// matching happens before codegen.
		// For example: is_pointer_v<int*> = is_pointer_impl<int*>::value
		// After substitution, we need to instantiate is_pointer_impl<int*> which should
		// match the specialization pattern is_pointer_impl<T*> and inherit from true_type.
		if (new_initializer.has_value()) {
			FLASH_LOG(Templates, Trace, "Phase 3: Checking initializer for variable template '", template_name,
					  "', is ExpressionNode: ", new_initializer->is<ExpressionNode>());

			if (new_initializer->is<ExpressionNode>()) {
				const ExpressionNode& init_expr = new_initializer->as<ExpressionNode>();

				// Check if the initializer is a qualified identifier (e.g., Template<Args>::member)
				bool is_qual_id = std::holds_alternative<QualifiedIdentifierNode>(init_expr);
				FLASH_LOG(Templates, Trace, "Phase 3: Is QualifiedIdentifierNode: ", is_qual_id);

				if (is_qual_id) {
					const QualifiedIdentifierNode& qual_id = std::get<QualifiedIdentifierNode>(init_expr);

					// The struct/class name is the namespace handle's name
					// For "is_pointer_impl<int*>::value", the namespace name is "is_pointer_impl<int*>"
					NamespaceHandle ns_handle = qual_id.namespace_handle();
					FLASH_LOG(Templates, Trace, "Phase 3: Namespace handle depth: ", gNamespaceRegistry.getDepth(ns_handle));

					if (!ns_handle.isGlobal()) {
						// Get the struct name from the namespace handle
						std::string_view struct_name_view = gNamespaceRegistry.getName(ns_handle);

						FLASH_LOG(Templates, Trace, "Phase 3: Struct name from qualified ID: '", struct_name_view, "'");

						// The struct name might be a mangled template instantiation (hash-based)
						// Extract the base template name from metadata
						std::string_view template_name_to_lookup = struct_name_view;
						std::string_view base_name = extractBaseTemplateName(struct_name_view);
						if (!base_name.empty()) {
							template_name_to_lookup = base_name;
							FLASH_LOG(Templates, Trace, "Phase 3: Extracted template name: '", template_name_to_lookup, "'");
						}

						// Try to instantiate the struct/class referenced in the qualified identifier
						// Look it up to see if it's a template
						auto inner_template_opt = gTemplateRegistry.lookupTemplate(template_name_to_lookup);
						if (inner_template_opt.has_value()) {
							std::vector<TemplateTypeArg> inner_template_args = resolved_args;
							// The substituted qualifier already carries the pack-expanded
							// use-site arguments. Only fall back to the referenced
							// placeholder's stored arguments when those expanded
							// arguments do not name a registered specialization:
							// placeholder-stored args can predate pack expansion (an
							// empty expansion stored as one dependent argument) and
							// would derive a different instantiation identity.
							if (!gTemplateRegistry.lookupExactSpecialization(
									template_name_to_lookup,
									std::span<const TemplateTypeArg>(
										resolved_args.data(),
										resolved_args.size()))
									 .has_value()) {
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
							}

							// This is a template - try to instantiate it with the concrete arguments
							// Use the referenced placeholder's own template arguments so outer
							// defaults are only forwarded when the inner template actually uses them.
							FLASH_LOG(Templates, Trace, "Phase 3: Triggering instantiation of '", template_name_to_lookup,
									  "' with ", inner_template_args.size(), " args from variable template initializer");

							auto instantiated = try_instantiate_class_template(template_name_to_lookup, inner_template_args);
							if (instantiated.has_value() && instantiated->is<StructDeclarationNode>()) {
								// Add to AST so it gets codegen
								registerAndNormalizeLateMaterializedTopLevelNode(*instantiated);
							}
							// A cache hit inside try_instantiate_class_template returns
							// nullopt when the full specialization already exists; the
							// qualifier rewrite below must still run so the initializer
							// points at that existing instantiation instead of the
							// declaration-time dependent placeholder namespace.
							std::string_view instantiated_name = get_instantiated_class_name(template_name_to_lookup, inner_template_args);
							auto instantiated_type_it = getTypesByNameMap().find(
								StringTable::getOrInternStringHandle(instantiated_name));
							const bool instantiation_ready =
								instantiated.has_value() ||
								(instantiated_type_it != getTypesByNameMap().end() &&
								 instantiated_type_it->second != nullptr &&
								 instantiated_type_it->second->isTemplateInstantiation());
							if (instantiation_ready) {
								// Create a new qualified identifier with the updated namespace
								// Get the parent namespace and add the instantiated name as a child
								NamespaceHandle parent_ns = gNamespaceRegistry.getParent(ns_handle);
								StringHandle instantiated_name_handle = StringTable::getOrInternStringHandle(instantiated_name);
								NamespaceHandle new_ns_handle = gNamespaceRegistry.getOrCreateNamespace(parent_ns, instantiated_name_handle);

								// Create new qualified identifier node
								QualifiedIdentifierNode new_qual_id(new_ns_handle, qual_id.identifier_token());
								if (qual_id.has_template_arguments()) {
									std::vector<ASTNode> template_argument_nodes(
										qual_id.template_arguments().begin(),
										qual_id.template_arguments().end());
									new_qual_id.set_template_arguments(
										std::move(template_argument_nodes));
								}
								// Once the owner has been substituted to a different concrete
								// instantiation, the original dependent-qualified record must not
								// survive on the rebound identifier (same rule as
								// ExpressionSubstitutor::substituteQualifiedIdentifier). Keeping it
								// would make later constant-expression evaluation reinterpret the
								// rewritten `Instantiation$hash::member` through the stale dependent
								// placeholder owner.
								if (gNamespaceRegistry.getName(new_ns_handle) == struct_name_view) {
									if (const auto* dependent_record =
											qual_id.dependentQualifiedName()) {
										new_qual_id.setDependentQualifiedName(
											*dependent_record);
									}
								}
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
	FLASH_LOG(Templates, Trace, "instantiate_full_specialization called for: ", instantiated_name);

	if (!spec_node.is<StructDeclarationNode>()) {
		FLASH_LOG(Templates, Error, "Full specialization is not a StructDeclarationNode");
		return std::nullopt;
	}

	StructDeclarationNode& spec_struct = spec_node.as<StructDeclarationNode>();
	InlineVector<TemplateParameterNode, 4> no_template_params;
	auto register_exact_specialization_instantiation = [&]() {
		const StringHandle qualified_template_name =
			StringTable::getOrInternStringHandle(template_name);
		gTemplateRegistry.registerInstantiation(
			qualified_template_name,
			template_args,
			spec_node);
		if (size_t last_colon = template_name.rfind("::");
			last_colon != std::string_view::npos) {
			const StringHandle unqualified_template_name =
				StringTable::getOrInternStringHandle(template_name.substr(last_colon + 2));
			gTemplateRegistry.registerInstantiation(
				unqualified_template_name,
				template_args,
				spec_node);
		}
	};

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
					alias_registration_type_spec.set_size_in_bits(
						concrete_member_info->sizeInBits());
				}
			}
			if (!isConcreteAliasSemanticSource(alias_semantic_source)) {
				alias_semantic_source = nullptr;
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
					alias_type_info.setEnumInfo(EnumTypeInfo(*enum_info));
				}
			}
			getTypesByNameMap().insert_or_assign(qualified_alias_name, &alias_type_info);

			FLASH_LOG(Templates, Trace, "Registered type alias: ", StringTable::getStringView(qualified_alias_name),
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
					alias_registration_type_spec.set_size_in_bits(
						original_nested_info->sizeInBits());
				}
				if (!isConcreteAliasSemanticSource(alias_semantic_source)) {
					alias_semantic_source = nullptr;
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
		FLASH_LOG(Templates, Trace, "Full spec already instantiated: ", instantiated_name);

		// Even if the struct is already instantiated, we need to register type aliases
		// with qualified names if they haven't been registered yet
		register_type_aliases();
		register_nested_class_aliases();
		register_exact_specialization_instantiation();

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

	StructTypeInfo* struct_info = &struct_type_info.emplaceStructInfo(StringTable::getOrInternStringHandle(instantiated_name), spec_struct.default_access(), spec_struct.is_union(), decl_ns);
	struct_info->declaration_node = &spec_struct;

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
		// C++20 [dcl.ptr]/1: preserve pointee bounds bound by a parenthesized
		// declarator without scaling storage.
		std::vector<size_t> member_pointee_dimensions;
		if (type_spec.has_pointee_array_declarator() && !type_spec.array_dimensions().empty()) {
			member_pointee_dimensions.assign(
				type_spec.array_dimensions().begin(),
				type_spec.array_dimensions().end());
		}
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
			member_pointee_dimensions,
			type_spec.has_pointee_array_declarator(),
			static_cast<int>(type_spec.pointer_depth()),
			member_decl.bitfield_width,
			type_spec.has_function_signature() ? std::optional(type_spec.function_signature()) : std::nullopt,
			member_decl.is_no_unique_address);
	}

	// Copy static members. Prefer the specialization AST so we preserve in-class
	// initializers even when the parsed StructTypeInfo has not materialized them yet.
	if (!spec_struct.static_members().empty()) {
		for (const auto& static_member : spec_struct.static_members()) {
			FLASH_LOG(Templates, Trace, "Copying static member: ", StringTable::getStringView(static_member.name));
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
				static_member.initializerDefinitionLookupContext(),
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
					FLASH_LOG(Templates, Trace, "Copying static member: ", static_member.getName());
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
	struct_info->classifyParticipation(currentStructEntityParticipation(false));
	FLASH_LOG(Templates, Trace, "Full spec has constructor: ", has_constructor ? "yes" : "no, needs default");

	struct_type_info.attachStructInfo(*struct_info);
	struct_type_info.bindStructInfoOwnership();
	if (struct_type_info.getStructInfo()) {
		struct_type_info.fallback_size_bits_ = struct_type_info.getStructInfo()->sizeInBits().value;
	}
	register_exact_specialization_instantiation();

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
	return evaluateDependentNTTPExpression(
		dependent_expr,
		template_params,
		template_args,
		StringHandle{});
}

std::optional<TemplateTypeArg> Parser::evaluateDependentNTTPExpression(
	const ASTNode& dependent_expr,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args,
	StringHandle explicit_substitution_owner) {
	if (dependent_expr.is<ExpressionNode>()) {
		const ExpressionNode& expression = dependent_expr.as<ExpressionNode>();
		if (std::holds_alternative<QualifiedIdentifierNode>(expression) ||
			std::holds_alternative<StaticCastNode>(expression)) {
			if (auto folded = tryFoldDependentQualifiedStaticMemberNTTPFromExpression(
					expression,
					template_params,
					template_args,
					true);
				folded.has_value()) {
				return folded;
			}
		}
	}
	StringHandle substitution_owner;
	if (explicit_substitution_owner.isValid()) {
		substitution_owner = explicit_substitution_owner;
	} else if (current_instantiation_ctx_ != nullptr &&
			   current_instantiation_ctx_->origin_name.isValid()) {
		substitution_owner = current_instantiation_ctx_->origin_name;
	} else if (!struct_parsing_context_stack_.empty()) {
		substitution_owner = StringTable::getOrInternStringHandle(
			struct_parsing_context_stack_.back().struct_name);
	} else if (!member_function_context_stack_.empty()) {
		substitution_owner = member_function_context_stack_.back().struct_name;
	}
	if (current_instantiation_ctx_ != nullptr &&
		current_instantiation_ctx_->origin_name.isValid() &&
		current_instantiation_ctx_->origin_name != substitution_owner) {
		const TypeInfo* owner_type_info = substitution_owner.isValid()
			? findTypeByName(substitution_owner)
			: nullptr;
		const auto owner_has_usable_inst_context =
			[&](const TypeInfo* type_info) {
			if (type_info == nullptr ||
				!type_info->hasInstantiationContext() ||
				type_info->instantiationContext() == nullptr) {
				return false;
			}
			const TypeInfo::InstantiationContext* inst_ctx =
				type_info->instantiationContext();
			for (const TypeInfo::TemplateArgInfo& arg_info : inst_ctx->param_args()) {
				TemplateTypeArg arg = toTemplateTypeArg(arg_info);
				if (arg.is_dependent || arg.dependent_name.isValid()) {
					continue;
				}
				if (arg.is_value || arg.type_index.is_valid()) {
					return true;
				}
			}
			return false;
		};
		if (!owner_has_usable_inst_context(owner_type_info)) {
			substitution_owner = current_instantiation_ctx_->origin_name;
		}
	}

	std::span<const TemplateParameterNode> effective_template_params = template_params;
	std::span<const TemplateTypeArg> effective_template_args = template_args;
	std::vector<TemplateParameterNode> effective_template_params_storage;
	std::vector<TemplateTypeArg> effective_template_args_storage;
	if (substitution_owner.isValid()) {
		std::string_view owner_name = StringTable::getStringView(substitution_owner);
		std::vector<std::optional<OuterTemplateBinding>> owner_outer_binding_storage;
		std::optional<OuterTemplateBinding> merged_owner_outer_binding;
		auto append_owner_chain_bindings = [&](StringHandle owner_handle) {
			if (!owner_handle.isValid()) {
				return;
			}
			std::string_view chain_owner_name = StringTable::getStringView(owner_handle);
			size_t search_pos = 0;
			while (search_pos <= chain_owner_name.size()) {
				size_t sep = chain_owner_name.find("::", search_pos);
				std::string_view owner_prefix = sep == std::string_view::npos
					? chain_owner_name
					: chain_owner_name.substr(0, sep);
				if (!owner_prefix.empty()) {
					const OuterTemplateBinding* owner_binding =
						gTemplateRegistry.getOuterTemplateBinding(owner_prefix);
					if (owner_binding != nullptr) {
						if (!merged_owner_outer_binding.has_value()) {
							merged_owner_outer_binding = OuterTemplateBinding();
						}
						appendMergedOuterTemplateBinding(
							*owner_binding,
							merged_owner_outer_binding.value());
					} else {
						owner_outer_binding_storage.push_back(
							buildOuterBindingForOwner(
								StringTable::getOrInternStringHandle(owner_prefix)));
						if (owner_outer_binding_storage.back().has_value()) {
							if (!merged_owner_outer_binding.has_value()) {
								merged_owner_outer_binding = OuterTemplateBinding();
							}
							appendMergedOuterTemplateBinding(
								owner_outer_binding_storage.back().value(),
								merged_owner_outer_binding.value());
						}
					}
				}
				if (sep == std::string_view::npos) {
					break;
				}
				search_pos = sep + 2;
			}
		};
		append_owner_chain_bindings(substitution_owner);
		if (current_instantiation_ctx_ != nullptr &&
			current_instantiation_ctx_->origin_name.isValid() &&
			current_instantiation_ctx_->origin_name != substitution_owner) {
			append_owner_chain_bindings(current_instantiation_ctx_->origin_name);
		}
		if (merged_owner_outer_binding.has_value() &&
			!merged_owner_outer_binding->params.empty()) {
			const OuterTemplateBinding& owner_outer_binding =
				merged_owner_outer_binding.value();
			const std::span<const TemplateTypeArg> owner_outer_args =
				!owner_outer_binding.all_args.empty()
					? std::span<const TemplateTypeArg>(
						owner_outer_binding.all_args.data(),
						owner_outer_binding.all_args.size())
					: std::span<const TemplateTypeArg>(
						owner_outer_binding.param_args.data(),
						owner_outer_binding.param_args.size());
			bool params_already_include_outer_prefix = false;
			if (owner_outer_binding.params.size() <= template_params.size()) {
				params_already_include_outer_prefix = true;
				for (size_t i = 0; i < owner_outer_binding.params.size(); ++i) {
					const TemplateParameterNode* owner_param =
						tryGetTemplateParameterNode(owner_outer_binding.params[i]);
					if (owner_param == nullptr ||
						template_params[i].nameHandle() != owner_param->nameHandle()) {
						params_already_include_outer_prefix = false;
						break;
					}
				}
			}
			bool args_already_include_outer_prefix = false;
			if (owner_outer_args.size() <= template_args.size()) {
				args_already_include_outer_prefix = true;
				for (size_t i = 0; i < owner_outer_args.size(); ++i) {
					if (!(template_args[i] == owner_outer_args[i])) {
						args_already_include_outer_prefix = false;
						break;
					}
				}
			}
			if (params_already_include_outer_prefix && !args_already_include_outer_prefix) {
				effective_template_args_storage.clear();
				effective_template_args_storage.reserve(template_params.size());
				size_t missing_prefix_args = template_params.size() > template_args.size()
					? template_params.size() - template_args.size()
					: 0;
				missing_prefix_args = std::min(missing_prefix_args, owner_outer_args.size());
				for (size_t i = 0; i < missing_prefix_args; ++i) {
					effective_template_args_storage.push_back(owner_outer_args[i]);
				}
				for (const TemplateTypeArg& arg : template_args) {
					effective_template_args_storage.push_back(arg);
				}
				effective_template_args = std::span<const TemplateTypeArg>(
					effective_template_args_storage.data(),
					effective_template_args_storage.size());
			} else if (!params_already_include_outer_prefix ||
					   !args_already_include_outer_prefix) {
				buildEffectiveVariableTemplateSubstitutionInputs(
					owner_name,
					&owner_outer_binding,
					template_params,
					template_args,
					effective_template_params_storage,
					effective_template_args_storage,
					effective_template_params,
					effective_template_args);
			}
		}
	}

	FlashCpp::ScopedStateCopy guard_substitutions(template_param_substitutions_);
	for (const ParserInstantiationContext* inst_ctx = current_instantiation_ctx_;
		 inst_ctx != nullptr;
		 inst_ctx = inst_ctx->parent) {
		if (!inst_ctx->origin_name.isValid()) {
			continue;
		}
		const TypeInfo* origin_type_info = findTypeByName(inst_ctx->origin_name);
		if (origin_type_info == nullptr ||
			!origin_type_info->hasInstantiationContext() ||
			origin_type_info->instantiationContext() == nullptr) {
			continue;
		}
		TemplateEnvironment inherited_env =
			buildTemplateEnvironment(*origin_type_info->instantiationContext());
		populateTemplateParamSubstitutions(
			template_param_substitutions_,
			inherited_env);
	}
	for (size_t i = 0; i < effective_template_params.size() && i < effective_template_args.size(); ++i) {
		const TemplateParameterNode& param = effective_template_params[i];
		const TemplateTypeArg& arg = effective_template_args[i];
		TemplateParamSubstitution direct_substitution;
		direct_substitution.param_name = param.nameHandle();
		if (param.kind() == TemplateParameterKind::Type && !arg.is_value) {
			direct_substitution.is_type_param = true;
			direct_substitution.substituted_type = arg;
			template_param_substitutions_.push_back(direct_substitution);
		} else if (param.kind() == TemplateParameterKind::NonType && arg.is_value) {
			direct_substitution.is_value_param = true;
			direct_substitution.value = arg.value;
			direct_substitution.value_type = arg.category();
			if (arg.has_typed_value_identity) {
				direct_substitution.typed_value_identity = arg.typed_value_identity;
			}
			template_param_substitutions_.push_back(direct_substitution);
		}
	}
	if (substitution_owner.isValid()) {
		const TypeInfo* owner_type_info = findTypeByName(substitution_owner);
		OuterTemplateBinding owner_binding = buildAccumulatedOuterTemplateBinding(
			owner_type_info,
			nullptr,
			substitution_owner);
		for (size_t binding_index = 0;
			 binding_index < owner_binding.param_names.size() &&
			 binding_index < owner_binding.param_args.size();
			 ++binding_index) {
			TemplateParamSubstitution owner_substitution;
			owner_substitution.param_name = owner_binding.param_names[binding_index];
			const TemplateTypeArg& owner_arg = owner_binding.param_args[binding_index];
			if (owner_arg.is_value) {
				owner_substitution.is_value_param = true;
				owner_substitution.value = owner_arg.value;
				owner_substitution.value_type = owner_arg.category();
				if (owner_arg.has_typed_value_identity) {
					owner_substitution.typed_value_identity = owner_arg.typed_value_identity;
				}
			} else {
				owner_substitution.is_type_param = true;
				owner_substitution.substituted_type = owner_arg;
			}
			template_param_substitutions_.push_back(owner_substitution);
		}
	}

	if (substitution_owner.isValid()) {
		if (const TypeInfo* owner_type_info = findTypeByName(substitution_owner);
			owner_type_info != nullptr &&
			owner_type_info->hasInstantiationContext() &&
			owner_type_info->instantiationContext() != nullptr) {
			TemplateEnvironment owner_environment = buildTemplateEnvironment(
				*owner_type_info->instantiationContext());
			populateTemplateParamSubstitutions(
				template_param_substitutions_,
				owner_environment);
		}
	}

	TypeIndex substitution_owner_type;
	if (substitution_owner.isValid()) {
		if (const TypeInfo* owner_type_info = findTypeByName(substitution_owner)) {
			substitution_owner_type =
				owner_type_info->registeredTypeIndex().withCategory(
					owner_type_info->typeEnum());
		}
	}
	ASTNode substituted = substituteTemplateParameters(
		dependent_expr,
		effective_template_params,
		effective_template_args,
		substitution_owner_type,
		false);

	if (substituted.is<ExpressionNode>()) {
		if (auto folded = tryFoldDependentQualifiedStaticMemberNTTPFromExpression(
				substituted.as<ExpressionNode>(),
				effective_template_params,
				effective_template_args,
				false);
			folded.has_value()) {
			return folded;
		}
	}

	// Evaluate the substituted expression using the standard constant expression evaluator
	ConstExpr::EvaluationContext eval_ctx(gSymbolTable, *this);
	eval_ctx.template_environment = buildTemplateEnvironment(
		effective_template_params,
		effective_template_args,
		nullptr);
	eval_ctx.template_args.assign(
		effective_template_args.begin(),
		effective_template_args.end());
	eval_ctx.template_param_names.reserve(effective_template_params.size());
	for (const TemplateParameterNode& template_param : effective_template_params) {
		eval_ctx.template_param_names.push_back(template_param.name());
	}
	ConstExpr::EvalResult result = ConstExpr::Evaluator::evaluate(substituted, eval_ctx);
	if (result.success()) {
		return templateTypeArgFromEvalResult(result);
	}

	FLASH_LOG(Templates, Trace,
		"evaluateDependentNTTPExpression: evaluation failed for dependent expression: ",
		result.error_message);
	return std::nullopt;
}
