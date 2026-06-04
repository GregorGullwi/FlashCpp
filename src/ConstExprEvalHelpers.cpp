#include "ConstExprEvalHelpers.h"

namespace ConstExpr {


std::string_view normalizeConstexprLookupName(std::string_view name) {
	if (const size_t materialized_suffix = name.find('$');
		materialized_suffix != std::string_view::npos) {
		return name.substr(0, materialized_suffix);
	}
	return name;
}

bool isRecoverablePointerDerefFailure(const EvalResult& result) {
	if (result.success()) {
		return false;
	}
	return result.error_message.rfind("Cannot dereference constexpr pointer:", 0) == 0;
}

bool sameConstexprMemberParameterTypes(
	const FunctionDeclarationNode& lhs,
	const FunctionDeclarationNode& rhs) {
	const auto& lhs_params = lhs.parameter_nodes();
	const auto& rhs_params = rhs.parameter_nodes();
	if (lhs_params.size() != rhs_params.size()) {
		return false;
	}
	for (size_t i = 0; i < lhs_params.size(); ++i) {
		if (!lhs_params[i].is<DeclarationNode>() || !rhs_params[i].is<DeclarationNode>()) {
			return false;
		}
		const TypeSpecifierNode& lhs_type = lhs_params[i].as<DeclarationNode>().type_specifier_node();
		const TypeSpecifierNode& rhs_type = rhs_params[i].as<DeclarationNode>().type_specifier_node();
		if (lhs_type.category() != rhs_type.category() ||
			lhs_type.cv_qualifier() != rhs_type.cv_qualifier() ||
			lhs_type.reference_qualifier() != rhs_type.reference_qualifier() ||
			lhs_type.pointer_levels().size() != rhs_type.pointer_levels().size() ||
			!std::ranges::equal(lhs_type.array_dimensions(), rhs_type.array_dimensions())) {
			return false;
		}
		for (size_t level = 0; level < lhs_type.pointer_levels().size(); ++level) {
			if (lhs_type.pointer_levels()[level].cv_qualifier !=
				rhs_type.pointer_levels()[level].cv_qualifier) {
				return false;
			}
		}
		TypeIndex lhs_index = lhs_type.type_index();
		TypeIndex rhs_index = rhs_type.type_index();
		if (lhs_index.needsTypeIndex() != rhs_index.needsTypeIndex()) {
			return false;
		}
		if (lhs_index.needsTypeIndex() && rhs_index.needsTypeIndex() &&
			lhs_index != rhs_index &&
			lhs_type.token().value() != rhs_type.token().value()) {
			return false;
		}
	}
	return true;
}

bool matchesConstexprFunctionName(
	const StructMemberFunction& member_func,
	StringHandle function_name_handle) {
	if (member_func.getName() == function_name_handle) {
		return true;
	}
	return normalizeConstexprLookupName(StringTable::getStringView(member_func.getName())) ==
		normalizeConstexprLookupName(StringTable::getStringView(function_name_handle));
}

bool sameConstexprFunctionIdentity(
	const FunctionDeclarationNode& lhs,
	const FunctionDeclarationNode& rhs) {
	if (normalizeConstexprLookupName(lhs.parent_struct_name()) !=
		normalizeConstexprLookupName(rhs.parent_struct_name())) {
		return false;
	}
	if (normalizeConstexprLookupName(lhs.decl_node().identifier_token().value()) !=
		normalizeConstexprLookupName(rhs.decl_node().identifier_token().value())) {
		return false;
	}
	if (lhs.parameter_nodes().size() != rhs.parameter_nodes().size()) {
		return false;
	}
	if (lhs.is_const_member_function() != rhs.is_const_member_function() ||
		lhs.is_static() != rhs.is_static()) {
		return false;
	}
	if (!sameConstexprMemberParameterTypes(lhs, rhs)) {
		return false;
	}
	if (lhs.has_mangled_name() && rhs.has_mangled_name() &&
		lhs.mangled_name() != rhs.mangled_name()) {
		return false;
	}
	return true;
}

const StructMemberFunction* findCollectedMemberFunctionByIdentity(
	std::span<const StructMemberFunction* const> candidates,
	const FunctionDeclarationNode& target_function) {
	for (const StructMemberFunction* candidate_member : candidates) {
		if (candidate_member == nullptr ||
			!candidate_member->function_decl.is<FunctionDeclarationNode>()) {
			continue;
		}
		const auto& candidate =
			candidate_member->function_decl.as<FunctionDeclarationNode>();
		if (sameConstexprFunctionIdentity(candidate, target_function)) {
			return candidate_member;
		}
	}
	return nullptr;
}


const FunctionDeclarationNode* findMatchingSymbolTableMemberDefinition(
	const FunctionDeclarationNode& target_function,
	EvaluationContext& context) {
	if (!context.symbols) {
		return nullptr;
	}
	const auto matches_candidate = [&](const FunctionDeclarationNode& candidate) {
		return candidate.parent_struct_name() == target_function.parent_struct_name() &&
			normalizeConstexprLookupName(candidate.decl_node().identifier_token().value()) ==
				normalizeConstexprLookupName(target_function.decl_node().identifier_token().value()) &&
			candidate.parameter_nodes().size() == target_function.parameter_nodes().size() &&
			candidate.is_const_member_function() == target_function.is_const_member_function() &&
			sameConstexprMemberParameterTypes(candidate, target_function);
	};
	const auto scan_owner_struct = [&](const SymbolTable& symbols) -> const FunctionDeclarationNode* {
		StringHandle owner_handle = StringTable::getOrInternStringHandle(target_function.parent_struct_name());
		std::optional<ASTNode> owner_symbol = symbols.lookup(owner_handle);
		if (!owner_symbol.has_value() || !owner_symbol->is<StructDeclarationNode>()) {
			return nullptr;
		}
		const auto& struct_node = owner_symbol->as<StructDeclarationNode>();
		for (const auto& member_func_decl : struct_node.member_functions()) {
			if (!member_func_decl.function_declaration.is<FunctionDeclarationNode>()) {
				continue;
			}
			const auto& candidate = member_func_decl.function_declaration.as<FunctionDeclarationNode>();
			if (matches_candidate(candidate)) {
				return &candidate;
			}
		}
		return nullptr;
	};
	if (const FunctionDeclarationNode* owner_candidate = scan_owner_struct(*context.symbols)) {
		return owner_candidate;
	}
	if (context.global_symbols && context.global_symbols != context.symbols) {
		if (const FunctionDeclarationNode* owner_candidate = scan_owner_struct(*context.global_symbols)) {
			return owner_candidate;
		}
	}
	auto overloads = context.symbols->lookup_all(target_function.decl_node().identifier_token().value());
	for (const ASTNode& overload : overloads) {
		if (overload.is<FunctionDeclarationNode>()) {
			const auto& candidate = overload.as<FunctionDeclarationNode>();
			if (matches_candidate(candidate)) {
				return &candidate;
			}
		}
	}
	if (context.global_symbols && context.global_symbols != context.symbols) {
		overloads = context.global_symbols->lookup_all(target_function.decl_node().identifier_token().value());
		for (const ASTNode& overload : overloads) {
			if (overload.is<FunctionDeclarationNode>()) {
				const auto& candidate = overload.as<FunctionDeclarationNode>();
				if (matches_candidate(candidate)) {
					return &candidate;
				}
			}
		}
	}
	return nullptr;
}

// Look up an enum member by name and return its constexpr value with an exact
// enum TypeSpecifierNode. Returns nullopt when the requested name is not an
// enumerator of the provided enum.
std::optional<EvalResult> tryEvaluateEnumConstant(
	const EnumTypeInfo& enum_info,
	TypeIndex enum_type_index,
	StringHandle enumerator_name) {
	const Enumerator* enumerator = enum_info.findEnumerator(enumerator_name);
	if (!enumerator) {
		return std::nullopt;
	}

	TypeSpecifierNode enum_type(TypeCategory::Enum, TypeQualifier::None, enum_info.sizeInBits(), Token{}, CVQualifier::None);
	enum_type.set_type_index(enum_type_index);
	EvalResult result = EvalResult::from_int(static_cast<long long>(enumerator->value));
	result.set_exact_type(enum_type);
	return result;
}

TemplateParameterKind inferTemplateBindingKindForLookup(const TemplateTypeArg& arg) {
	if (arg.is_template_template_arg) {
		return TemplateParameterKind::Template;
	}
	if (arg.is_value) {
		return TemplateParameterKind::NonType;
	}
	return TemplateParameterKind::Type;
}

// Build a TemplateEnvironment from the outer template bindings stored on a
// FunctionDeclarationNode.  This is the environment-aware equivalent of the
// old legacy_environment bridge: callers that set context.template_param_names
// and context.template_args from outer_template_param_names()/outer_template_args()
// must also call this to populate context.template_environment so that
// trySubstituteDependentTemplateArgForLookup can find the bindings without
// falling back to the now-removed legacy bridge.
TemplateEnvironment buildOuterFunctionTemplateEnvironment(
	const InlineVector<StringHandle, 4>& param_names,
	const InlineVector<TypeInfo::TemplateArgInfo, 4>& args) {
	if (param_names.size() != args.size()) {
		throw InternalError("buildOuterFunctionTemplateEnvironment: param_names.size() != args.size()");
	}
	TemplateEnvironment env;
	env.bindings.reserve(param_names.size());
	for (size_t i = 0; i < param_names.size(); ++i) {
		TemplateBinding binding;
		binding.name = param_names[i];
		TemplateTypeArg arg = toTemplateTypeArg(args[i]);
		binding.kind = inferTemplateBindingKindForLookup(arg);
		binding.is_pack = false;
		binding.args.push_back(std::move(arg));
		env.bindings.push_back(std::move(binding));
	}
	return env;
}

const StructMemberFunction* findMemberFunctionMetadataRecursive(
	const StructTypeInfo* struct_info,
	const FunctionDeclarationNode& target_function) {
	if (!struct_info) {
		return nullptr;
	}

	for (const auto& member_func : struct_info->member_functions) {
		if (!member_func.function_decl.is<FunctionDeclarationNode>()) {
			continue;
		}
		const auto& candidate = member_func.function_decl.as<FunctionDeclarationNode>();
		if (&candidate == &target_function || &candidate.decl_node() == &target_function.decl_node()) {
			return &member_func;
		}
		if (candidate.has_mangled_name() &&
			target_function.has_mangled_name() &&
			candidate.mangled_name() == target_function.mangled_name()) {
			return &member_func;
		}
		if (sameConstexprFunctionIdentity(candidate, target_function)) {
			return &member_func;
		}
	}

	for (const auto& base_class : struct_info->base_classes) {
		const TypeInfo* base_type_info = tryGetTypeInfo(base_class.type_index);
		if (!base_type_info) {
			continue;
		}
		const StructTypeInfo* base_struct_info = base_type_info->getStructInfo();
		if (!base_struct_info) {
			continue;
		}
		if (const StructMemberFunction* base_match =
				findMemberFunctionMetadataRecursive(base_struct_info, target_function)) {
			return base_match;
		}
	}

	return nullptr;
}

// Find the final overrider of `base_virtual` in `dynamic_struct_info` and its
// base classes.  Returns the most-derived StructMemberFunction that overrides
// `base_virtual` (same name and signature), or
// nullptr when no override exists (caller should keep the base declaration).
const StructMemberFunction* findFinalOverrider(
	const StructTypeInfo* dynamic_struct_info,
	const StructMemberFunction& base_virtual,
	size_t param_count) {
	if (!dynamic_struct_info) {
		return nullptr;
	}

	const FunctionDeclarationNode* base_virtual_decl =
		base_virtual.function_decl.is<FunctionDeclarationNode>()
			? &base_virtual.function_decl.as<FunctionDeclarationNode>()
			: nullptr;
	if (base_virtual_decl &&
		normalizeConstexprLookupName(base_virtual_decl->parent_struct_name()) ==
			normalizeConstexprLookupName(StringTable::getStringView(dynamic_struct_info->name))) {
		return nullptr;
	}

	for (const auto& mf : dynamic_struct_info->member_functions) {
		if (mf.name != base_virtual.name) {
			continue;
		}
		// Don't return the base virtual itself as its own "override".
		if (&mf == &base_virtual) {
			continue;
		}
		if (!mf.function_decl.is<FunctionDeclarationNode>()) {
			continue;
		}
		const auto& fd = mf.function_decl.as<FunctionDeclarationNode>();
		if (base_virtual_decl &&
			sameConstexprFunctionIdentity(fd, *base_virtual_decl)) {
			continue;
		}
		const std::string_view dynamic_owner_name =
			normalizeConstexprLookupName(StringTable::getStringView(dynamic_struct_info->name));
		const std::string_view candidate_owner_name =
			normalizeConstexprLookupName(fd.parent_struct_name());
		// Skip inherited/base declarations that are mirrored into the derived
		// member list. Final overrider selection must only consider functions
		// declared by the currently inspected dynamic class at this recursion step.
		if (candidate_owner_name != dynamic_owner_name) {
			continue;
		}
		if (fd.parameter_nodes().size() != param_count) {
			continue;
		}
		if (!base_virtual.function_decl.is<FunctionDeclarationNode>()) {
			continue;
		}
		const auto& base_fd = base_virtual.function_decl.as<FunctionDeclarationNode>();
		if (fd.is_const_member_function() != base_fd.is_const_member_function()) {
			continue;
		}
		if (!sameConstexprMemberParameterTypes(fd, base_fd)) {
			continue;
		}
		return &mf;
	}

	// Recurse into base classes (most-derived first) to find overrides in
	// intermediate base classes.
	for (const auto& base_class : dynamic_struct_info->base_classes) {
		const TypeInfo* bti = tryGetTypeInfo(base_class.type_index);
		if (!bti) {
			continue;
		}
		const StructTypeInfo* bsi = bti->getStructInfo();
		if (!bsi) {
			continue;
		}
		if (const StructMemberFunction* found = findFinalOverrider(bsi, base_virtual, param_count)) {
			return found;
		}
	}

	return nullptr;
}

InlineVector<TemplateParameterNode, 4> getTemplateParametersForTypeInfo(
	const TypeInfo& owner_type_info,
	Parser& parser_context) {
	const StringHandle qualified_template_name =
		gNamespaceRegistry.buildQualifiedIdentifier(
			owner_type_info.sourceNamespace(),
			owner_type_info.baseTemplateName());

	// Use parser-built semantic requests so that PointOfInstantiation timing,
	// current namespace context, and definition-namespace from any active
	// template definition lookup context are all taken into account.
	const StringHandle base_template_name = owner_type_info.baseTemplateName();
	TemplateNameLookupResult qualifiedLookup = gTemplateRegistry.lookupTemplateName(
		parser_context.makeTemplateNameLookupRequest(
			qualified_template_name,
			TemplateNameLookupKind::Qualified,
			false));
	TemplateNameLookupResult ordinaryLookup = gTemplateRegistry.lookupTemplateName(
		parser_context.makeTemplateNameLookupRequest(
			base_template_name,
			TemplateNameLookupKind::Ordinary,
			false));

	// Alias-template and class-template extraction share the same shape:
	// select the first declaration by kind and return template parameters when
	// the declaration node type matches.
	auto tryGetParams = [&](const TemplateNameLookupResult& lookup_result,
							TemplateDeclarationKind kind,
							auto paramExtractor)
		-> std::optional<InlineVector<TemplateParameterNode, 4>> {
		std::optional<ASTNode> decl = lookup_result.firstDeclarationOfKind(kind);
		if (decl.has_value()) {
			return paramExtractor(*decl);
		}
		return std::nullopt;
	};
	auto aliasAccessor = [](const ASTNode& n) -> std::optional<InlineVector<TemplateParameterNode, 4>> {
		if (n.is<TemplateAliasNode>()) {
			return n.as<TemplateAliasNode>().template_parameters();
		}
		return std::nullopt;
	};
	auto classAccessor = [](const ASTNode& n) -> std::optional<InlineVector<TemplateParameterNode, 4>> {
		if (n.is<TemplateClassDeclarationNode>()) {
			return n.as<TemplateClassDeclarationNode>().template_parameters();
		}
		return std::nullopt;
	};

	if (auto params = tryGetParams(qualifiedLookup, TemplateDeclarationKind::AliasTemplate, aliasAccessor)) {
		return std::move(*params);
	}
	if (auto params = tryGetParams(ordinaryLookup, TemplateDeclarationKind::AliasTemplate, aliasAccessor)) {
		return std::move(*params);
	}
	if (auto params = tryGetParams(qualifiedLookup, TemplateDeclarationKind::ClassTemplate, classAccessor)) {
		return std::move(*params);
	}
	if (auto params = tryGetParams(ordinaryLookup, TemplateDeclarationKind::ClassTemplate, classAccessor)) {
		return std::move(*params);
	}
	return {};
}

std::optional<TemplateTypeArg> trySubstituteDependentTemplateArgForLookup(
	const TemplateTypeArg& dependent_arg,
	EvaluationContext& context,
	const TypeInfo* owner_type_info,
	int recursion_depth) {
	constexpr int kMaxDependentLookupMaterializationDepth = 32;
	if (recursion_depth > kMaxDependentLookupMaterializationDepth) {
		return std::nullopt;
	}
	TemplateEnvironment context_environment;
	bool context_environment_initialized = false;
	TemplateEnvironment owner_environment;
	bool owner_environment_initialized = false;
	const auto ensure_context_environment = [&]() -> const TemplateEnvironment* {
		if (context_environment_initialized) {
			return (!context_environment.bindings.empty() || context_environment.parent != nullptr)
				? &context_environment
				: nullptr;
		}
		context_environment_initialized = true;
		if (context.template_environment.bindings.empty() &&
			context.template_environment.parent == nullptr) {
			return nullptr;
		}
		context_environment = context.template_environment;
		return &context_environment;
	};
	const auto ensure_owner_environment = [&]() -> const TemplateEnvironment* {
		if (owner_environment_initialized) {
			return (!owner_environment.bindings.empty() || owner_environment.parent != nullptr)
				? &owner_environment
				: nullptr;
		}
		owner_environment_initialized = true;
		if (owner_type_info == nullptr || !owner_type_info->hasInstantiationContext()) {
			return nullptr;
		}
		owner_environment = buildTemplateEnvironment(*owner_type_info->instantiationContext());
		if (const TemplateEnvironment* context_env = ensure_context_environment();
			context_env != nullptr) {
			owner_environment.parent = context_env;
		}
		return &owner_environment;
	};
	const auto select_lookup_environment = [&]() -> const TemplateEnvironment* {
		if (const TemplateEnvironment* owner_env = ensure_owner_environment();
			owner_env != nullptr) {
			return owner_env;
		}
		if (const TemplateEnvironment* context_env = ensure_context_environment();
			context_env != nullptr) {
			return context_env;
		}
		return nullptr;
	};

	StringHandle lookup_name = dependent_arg.dependent_name;
	if (!lookup_name.isValid() &&
		!dependent_arg.is_value &&
		dependent_arg.type_index.is_valid()) {
		if (const TypeInfo* arg_type_info = tryGetTypeInfo(dependent_arg.type_index);
			arg_type_info != nullptr && arg_type_info->name().isValid()) {
			if (const TemplateEnvironment* lookup_environment = select_lookup_environment();
				lookup_environment != nullptr) {
				if (resolveContextBinding(arg_type_info->name(), *lookup_environment).has_value() ||
					lookup_environment->findOne(arg_type_info->name()) != nullptr) {
					lookup_name = arg_type_info->name();
				}
			}
		}
	}
	const bool can_try_dependent_expression_substitution =
		dependent_arg.is_value &&
		dependent_arg.dependent_expr.has_value() &&
		context.parser != nullptr;
	if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
		FLASH_LOG(ConstExpr, Debug, "trySubstituteDependentTemplateArgForLookup: is_value=", dependent_arg.is_value,
			", is_dependent=", dependent_arg.is_dependent,
			", dep_name=", StringTable::getStringView(dependent_arg.dependent_name),
			", lookup_name=", StringTable::getStringView(lookup_name),
			", type_index=", dependent_arg.type_index,
			", owner_has_inst_ctx=", (owner_type_info != nullptr && owner_type_info->hasInstantiationContext()));
		if (owner_type_info != nullptr && owner_type_info->hasInstantiationContext()) {
			const auto* inst_ctx = owner_type_info->instantiationContext();
			for (size_t i = 0; i < inst_ctx->param_names.size() && i < inst_ctx->param_args.size(); ++i) {
				const TemplateTypeArg arg = toTemplateTypeArg(inst_ctx->param_args[i]);
				FLASH_LOG(ConstExpr, Debug, "  owner inst_ctx[", i, "] name=",
					StringTable::getStringView(inst_ctx->param_names[i]),
					", is_value=", arg.is_value,
					", is_dependent=", arg.is_dependent,
					", dep_name=", StringTable::getStringView(arg.dependent_name),
					", type_index=", arg.type_index);
			}
		}
	}
	if (!lookup_name.isValid() && !can_try_dependent_expression_substitution) {
		return std::nullopt;
	}

	auto resolve_from_environment =
		[&](const TemplateEnvironment& environment) -> std::optional<TemplateTypeArg> {
		if (!lookup_name.isValid()) {
			return std::nullopt;
		}
		std::optional<TemplateTypeArg> resolved = resolveContextBinding(
			lookup_name,
			environment);
		if (!resolved.has_value()) {
			if (const TemplateTypeArg* direct_binding =
					environment.findOne(lookup_name);
				direct_binding != nullptr) {
				resolved = *direct_binding;
			}
		}
		return resolved;
	};

	std::optional<TemplateTypeArg> resolved_binding;
	const TemplateEnvironment* resolved_environment = nullptr;
	if (lookup_name.isValid()) {
		if (const TemplateEnvironment* lookup_environment = select_lookup_environment();
			lookup_environment != nullptr) {
			resolved_binding = resolve_from_environment(*lookup_environment);
			if (resolved_binding.has_value()) {
				if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
					FLASH_LOG(ConstExpr, Debug, "lookup binding resolved for '",
						StringTable::getStringView(lookup_name),
						"': is_value=", resolved_binding->is_value,
						", is_dependent=", resolved_binding->is_dependent,
						", dep_name=", StringTable::getStringView(resolved_binding->dependent_name),
						", type_index=", resolved_binding->type_index);
				}
				resolved_environment = lookup_environment;
			}
		}
	}

	if (can_try_dependent_expression_substitution) {
		const TemplateEnvironment* evaluation_environment = resolved_environment;
		if (evaluation_environment == nullptr) {
			evaluation_environment = select_lookup_environment();
		}
		if (evaluation_environment != nullptr) {
			Parser& parser = *context.parser;
			ExpressionSubstitutor substitutor(*evaluation_environment, parser);
			ASTNode substituted_expr = substitutor.substitute(*dependent_arg.dependent_expr);
			auto saved_template_environment = context.template_environment;
			context.template_environment = *evaluation_environment;
			EvalResult evaluated = Evaluator::evaluate(substituted_expr, context);
			context.template_environment = std::move(saved_template_environment);
			if (evaluated.success()) {
				TemplateTypeArg evaluated_arg = templateTypeArgFromEvalResult(evaluated);
				if (evaluated_arg.category() == TypeCategory::Invalid &&
					dependent_arg.category() != TypeCategory::Invalid) {
					evaluated_arg.setCategory(dependent_arg.category());
				}
				return evaluated_arg;
			}
			TemplateTypeArg substituted_dependent_arg = dependent_arg;
			substituted_dependent_arg.dependent_expr = std::move(substituted_expr);
			substituted_dependent_arg.is_dependent = true;
			return substituted_dependent_arg;
		}
	}

	if (!resolved_binding.has_value()) {
		if (lookup_name.isValid() &&
			owner_type_info != nullptr &&
			owner_type_info->isTemplateInstantiation() &&
			context.parser != nullptr) {
			Parser& parser = *context.parser;
			InlineVector<TemplateParameterNode, 4> owner_template_params =
				getTemplateParametersForTypeInfo(*owner_type_info, parser);
			const size_t owner_pair_count = std::min(
				owner_template_params.size(),
				owner_type_info->templateArgs().size());
			for (size_t i = 0; i < owner_pair_count; ++i) {
				if (owner_template_params[i].name() !=
					StringTable::getStringView(lookup_name)) {
					continue;
				}
				TemplateTypeArg owner_bound_arg =
					toTemplateTypeArg(owner_type_info->templateArgs()[i]);
				if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
					FLASH_LOG(ConstExpr, Debug, "owner param positional rebound for '",
						owner_template_params[i].name(),
						"': is_value=", owner_bound_arg.is_value,
						", is_dependent=", owner_bound_arg.is_dependent,
						", dep_name=", StringTable::getStringView(owner_bound_arg.dependent_name),
						", type_index=", owner_bound_arg.type_index);
				}
				if (std::optional<TemplateTypeArg> rematerialized =
						trySubstituteDependentTemplateArgForLookup(
							owner_bound_arg,
							context,
							owner_type_info,
							recursion_depth + 1);
					rematerialized.has_value()) {
					owner_bound_arg = std::move(*rematerialized);
				}
				if (!dependent_arg.is_value && !owner_bound_arg.is_value) {
					return rebindDependentTemplateTypeArg(
						owner_bound_arg,
						dependent_arg);
				}
				if (dependent_arg.is_value &&
					dependent_arg.dependent_expr.has_value() &&
					!owner_bound_arg.is_value) {
					return std::nullopt;
				}
				return owner_bound_arg;
			}
		}
		return std::nullopt;
	}
	if (!dependent_arg.is_value && !resolved_binding->is_value) {
		TemplateTypeArg materialized_binding = *resolved_binding;
		const TemplateEnvironment* lookup_environment = resolved_environment;
		if (lookup_environment == nullptr) {
			lookup_environment = select_lookup_environment();
		}
		if (lookup_environment != nullptr) {
			constexpr size_t kMaxBindingMaterializationDepth = 16;
			size_t materialization_depth = 0;
			while (materialization_depth < kMaxBindingMaterializationDepth) {
				bool advanced = false;
				if (materialized_binding.is_dependent &&
					materialized_binding.dependent_name.isValid()) {
					if (auto rebound_binding = resolveContextBinding(
							materialized_binding.dependent_name,
							*lookup_environment);
						rebound_binding.has_value() && !rebound_binding->is_value) {
						materialized_binding = rebindDependentTemplateTypeArg(
							*rebound_binding,
							materialized_binding);
						advanced = true;
					}
				}
				if (!advanced &&
					materialized_binding.type_index.is_valid()) {
					if (const TypeInfo* materialized_type_info = tryGetTypeInfo(materialized_binding.type_index);
						materialized_type_info != nullptr &&
						materialized_type_info->isDependentPlaceholder()) {
						if (auto rebound_binding = resolveContextBinding(
								materialized_type_info->name(),
								*lookup_environment);
							rebound_binding.has_value() && !rebound_binding->is_value) {
							materialized_binding = rebindDependentTemplateTypeArg(
								*rebound_binding,
								materialized_binding);
							advanced = true;
						}
					}
				}
				if (!advanced) {
					break;
				}
				++materialization_depth;
			}
		}
		return rebindDependentTemplateTypeArg(materialized_binding, dependent_arg);
	}

	if (dependent_arg.is_value && !resolved_binding->is_value) {
		return std::nullopt;
	}

	return *resolved_binding;
}


const StructMember* findMemberInfoRecursive(const StructTypeInfo* struct_info, StringHandle member_name_handle) {
	if (!struct_info) {
		return nullptr;
	}

	for (const auto& member : struct_info->members) {
		if (member.getName() == member_name_handle) {
			return &member;
		}
	}

	for (const auto& base : struct_info->base_classes) {
		if (const TypeInfo* base_type = tryGetTypeInfo(base.type_index);
			base_type && base_type->getStructInfo()) {
			if (const StructMember* base_member = findMemberInfoRecursive(base_type->getStructInfo(), member_name_handle)) {
				return base_member;
			}
		}
	}

	return nullptr;
}

TypeSpecifierNode makeMemberTypeSpecForDefaultInit(const StructMember& member) {
	TypeSpecifierNode type_spec(
		member.type_index,
		TypeQualifier::None,
		SizeInBits{static_cast<int>(member.size * 8)},
		Token{},
		CVQualifier::None);
	type_spec.set_reference_qualifier(member.reference_qualifier);
	type_spec.add_pointer_levels(member.pointer_depth);
	type_spec.set_array_dimensions(member.array_dimensions);
	if (member.function_signature.has_value()) {
		type_spec.set_function_signature(*member.function_signature);
	}
	return type_spec;
}

TypeSpecifierNode makeTypeSpecForDefaultInit(TypeIndex type_index) {
	const TypeInfo* type_info = tryGetTypeInfo(type_index);
	const SizeInBits size_in_bits = type_info ? type_info->sizeInBits() : SizeInBits{0};
	return TypeSpecifierNode(
		type_index,
		TypeQualifier::None,
		size_in_bits,
		Token{},
		CVQualifier::None);
}

EvalResult makeConstructorDefaultInitFromType(const TypeSpecifierNode& type_spec, EvaluationContext& context) {
	if (type_spec.is_array()) {
		std::span<const size_t> dims = type_spec.array_dimensions();
		if (dims.empty()) {
			return EvalResult::error("Missing dimensions for default-initialized array member");
		}

		EvalResult array_result = EvalResult::from_int(0LL);
		array_result.is_array = true;
		array_result.is_indeterminate = false;
		array_result.set_exact_type(type_spec);

		TypeSpecifierNode element_type = type_spec;
		if (dims.size() <= 1) {
			element_type.set_array_dimensions({});
		} else {
			element_type.set_array_dimensions(std::vector<size_t>(dims.begin() + 1, dims.end()));
		}
		element_type.set_size_in_bits(getTypeSpecSizeBits(element_type));

		array_result.array_elements.reserve(dims[0]);
		for (size_t i = 0; i < dims[0]; ++i) {
			EvalResult element_result = makeConstructorDefaultInitFromType(element_type, context);
			if (!element_result.success()) {
				return element_result;
			}
			array_result.is_indeterminate = array_result.is_indeterminate || element_result.is_indeterminate;
			array_result.array_elements.push_back(std::move(element_result));
		}
		return array_result;
	}

	if (is_struct_type(type_spec.category())) {
		const TypeInfo* type_info = tryGetTypeInfo(type_spec.type_index());
		const StructTypeInfo* struct_info = type_info ? type_info->getStructInfo() : nullptr;
		if (!struct_info) {
			return EvalResult::error("Struct type info not found for default-initialized member");
		}

		if (struct_info->hasUserDefinedConstructor()) {
			ChunkedVector<ASTNode> empty_args;
			auto ctor_result = Evaluator::try_materialize_struct_from_ctor_args(
				struct_info,
				type_spec.type_index(),
				empty_args,
				context,
				true,
				nullptr,
				nullptr,
				false);
			if (!ctor_result.has_value()) {
				return EvalResult::error(
					"No matching default constructor for '" +
					std::string(StringTable::getStringView(struct_info->getName())) + "'");
			}
			return *ctor_result;
		}

		std::unordered_map<std::string_view, EvalResult> base_bindings;
		bool base_is_indeterminate = false;
		for (const auto& base : struct_info->base_classes) {
			EvalResult base_result = makeConstructorDefaultInitFromType(makeTypeSpecForDefaultInit(base.type_index), context);
			if (!base_result.success()) {
				return base_result;
			}
			for (const auto& [base_member_name, base_member_value] : base_result.object_member_bindings) {
				if (base_bindings.find(base_member_name) == base_bindings.end()) {
					base_bindings[base_member_name] = base_member_value;
				}
			}
			base_is_indeterminate = base_is_indeterminate || base_result.is_indeterminate;
		}

		InitializerListNode empty_init_list;
		EvalResult object_result = Evaluator::materialize_aggregate_object_value(
			struct_info,
			type_spec.type_index(),
			empty_init_list,
			context,
			base_bindings.empty() ? nullptr : &base_bindings);
		if (!object_result.success()) {
			return object_result;
		}
		object_result.is_indeterminate = base_is_indeterminate;

		for (const auto& [base_member_name, base_member_value] : base_bindings) {
			if (object_result.object_member_bindings.find(base_member_name) == object_result.object_member_bindings.end()) {
				object_result.object_member_bindings[base_member_name] = base_member_value;
			}
		}

		for (const auto& nested_member : struct_info->members) {
			std::string_view nested_name = StringTable::getStringView(nested_member.getName());
			if (object_result.object_member_bindings.find(nested_name) != object_result.object_member_bindings.end()) {
				object_result.is_indeterminate =
					object_result.is_indeterminate || object_result.object_member_bindings[nested_name].is_indeterminate;
				continue;
			}
			EvalResult nested_result = makeConstructorMemberDefaultInit(nested_member, context);
			if (!nested_result.success()) {
				return nested_result;
			}
			object_result.is_indeterminate = object_result.is_indeterminate || nested_result.is_indeterminate;
			object_result.object_member_bindings[nested_name] = std::move(nested_result);
		}
		return object_result;
	}

	EvalResult result = EvalResult::indeterminate();
	result.set_exact_type(type_spec);
	return result;
}


std::optional<TypeSpecifierNode> try_get_type_from_eval_result(const EvalResult& value) {
	if (value.exact_type.has_value()) {
		return value.exact_type;
	}

	if (const TypeInfo* type_info = tryGetTypeInfo(value.object_type_index)) {
		return TypeSpecifierNode(value.object_type_index.withCategory(type_info->typeEnum()), type_info->sizeInBits(), Token{}, CVQualifier::None, ReferenceQualifier::None);
	}

	if (std::holds_alternative<bool>(value.value)) {
		return TypeSpecifierNode(TypeCategory::Bool, TypeQualifier::None, 8, Token{}, CVQualifier::None);
	}
	if (std::holds_alternative<long long>(value.value)) {
		return TypeSpecifierNode(TypeCategory::LongLong, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	}
	if (value.is_uint()) {
		return TypeSpecifierNode(TypeCategory::UnsignedLongLong, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	}
	if (std::holds_alternative<double>(value.value)) {
		return TypeSpecifierNode(TypeCategory::Double, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	}

	return std::nullopt;
}

EvalResult read_bound_identifier_value(
	const EvalResult& bound_value,
	std::string_view name,
	const std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context) {
	if (isReferenceAliasBinding(bound_value)) {
		const EvalResult* referenced_value = resolveReadThroughReferenceAlias(bound_value, bindings, context);
		if (!referenced_value) {
			return EvalResult::error("Dangling reference binding in constant expression: " + std::string(name));
		}
		return validateConstexprRead(*referenced_value);
	}

	if (bound_value.is_array) {
		if (!bound_value.array_origin_var.isValid()) {
			EvalResult tagged = bound_value;
			tagged.array_origin_var = StringTable::getOrInternStringHandle(name);
			return tagged;
		}
		return bound_value;
	}

	// For struct containers, the container-level is_indeterminate flag may be stale
	// after individual member assignments (e.g., `S s; s.x = 42; return s;`).
	// Perform a deep check: only reject if any individual member is still indeterminate.
	if (bound_value.is_indeterminate && bound_value.object_type_index.is_valid() &&
		!bound_value.object_member_bindings.empty()) {
		for (const auto& [_, member_val] : bound_value.object_member_bindings) {
			if (member_val.is_indeterminate) {
				return validateConstexprRead(member_val);
			}
		}
		// All members are determinate — the container flag is stale, allow the read.
		return bound_value;
	}

	return validateConstexprRead(bound_value);
}

std::optional<TypeSpecifierNode> try_get_promoted_shift_operand_type(const EvalResult& value) {
	auto type_opt = try_get_type_from_eval_result(value);
	if (!type_opt.has_value()) {
		return std::nullopt;
	}

	const TypeSpecifierNode& type_spec = *type_opt;
	if (!isIntegralType(type_spec.category())) {
		return std::nullopt;
	}

	const TypeCategory promoted_type = promote_integer_type(type_spec.category());
	const SizeInBits promoted_width{get_type_size_bits(promoted_type)};
	if (promoted_width.is_set()) {
		return TypeSpecifierNode(promoted_type, TypeQualifier::None, promoted_width, Token{}, CVQualifier::None);
	}

	// Defensive fallback for unusual/dependent type shapes where the promoted
	// type is known but the width table cannot provide a concrete bit-size yet.
	if (type_spec.size_in_bits() > 0) {
		return TypeSpecifierNode(promoted_type, TypeQualifier::None, type_spec.sizeBits(), Token{}, CVQualifier::None);
	}

	return std::nullopt;
}

ShiftEvaluationInfo get_shift_evaluation_info(const EvalResult& value) {
	ShiftEvaluationInfo info;
	info.promoted_type = try_get_promoted_shift_operand_type(value);
	if (info.promoted_type) {
		info.width_bits = normalize_shift_width(info.promoted_type->size_in_bits());
	}
	return info;
}

EvalResult make_shift_result(const std::optional<TypeSpecifierNode>& promoted_type, unsigned long long value) {
	EvalResult result = EvalResult::from_uint(value);
	if (promoted_type) {
		result.set_exact_type(*promoted_type);
	}
	return result;
}

EvalResult make_shift_result(const std::optional<TypeSpecifierNode>& promoted_type, long long value) {
	EvalResult result = EvalResult::from_int(value);
	if (promoted_type) {
		result.set_exact_type(*promoted_type);
	}
	return result;
}

TypeSpecifierNode makeAggregateMemberTypeSpec(const StructMember& member_info) {
	TypeSpecifierNode member_type(
		member_info.type_index.withCategory(member_info.memberType()),
		static_cast<int>(member_info.referenced_size_bits),
		Token{},
		CVQualifier::None,
		member_info.reference_qualifier);
	member_type.set_type_index(member_info.type_index);
	if (member_info.is_array) {
		member_type.set_array_dimensions(member_info.array_dimensions);
	}
	if (member_info.pointer_depth > 0) {
		member_type.add_pointer_levels(member_info.pointer_depth);
	}
	return member_type;
}

EvalResult applyAggregateMemberScalarInitialization(
	const StructMember& member_info,
	EvalResult value,
	bool enforce_list_narrowing) {
	if (!value.success() ||
		member_info.is_array ||
		is_struct_type(member_info.type_index.category()) ||
		member_info.pointer_depth > 0 ||
		member_info.reference_qualifier != ReferenceQualifier::None) {
		return value;
	}

	const TypeSpecifierNode member_type = makeAggregateMemberTypeSpec(member_info);
	const TypeCategory target_category = member_type.category();
	if (!(isIntegralType(target_category) ||
		  isFloatingPointType(target_category) ||
		  target_category == TypeCategory::Enum)) {
		return value;
	}

	if (enforce_list_narrowing) {
		const TypeCategory source_category =
			BuiltinListInitNarrowing::effectiveScalarCategory(value);
		const TypeCategory effective_target_category =
			BuiltinListInitNarrowing::effectiveScalarCategory(member_type);
		if (BuiltinListInitNarrowing::isNarrowingConversion(
				source_category,
				effective_target_category,
				value)) {
			return EvalResult::error(
				"Narrowing conversion in direct-list-initialization",
				EvalErrorType::NotConstantExpression);
		}
	}

	return Evaluator::convertEvalResultToTargetType(
		member_type,
		value,
		"Unsupported aggregate member type in constant evaluation");
}


std::optional<size_t> try_get_constexpr_pointer_upper_bound(
	std::string_view var_name,
	EvaluationContext* context,
	const std::unordered_map<std::string_view, EvalResult>* bindings) {
	if (bindings) {
		auto binding_it = bindings->find(var_name);
		if (binding_it != bindings->end()) {
			const EvalResult& bound = binding_it->second;
			if (bound.is_array) {
				if (!bound.array_elements.empty()) {
					return bound.array_elements.size();
				}
				if (!bound.array_values.empty()) {
					return bound.array_values.size();
				}
				// Array with unknown size — cannot determine upper bound.
				return std::nullopt;
			}
			// Non-array binding: scalar object, valid range is [0, 1].
			return size_t{1};
		}
	}

	if (!context || !context->symbols) {
		return std::nullopt;
	}

	auto symbol = context->symbols->lookup(var_name);
	if (!symbol.has_value() && context->global_symbols) {
		symbol = context->global_symbols->lookup(var_name);
	}
	if (!symbol.has_value() || !symbol->is<VariableDeclarationNode>()) {
		return std::nullopt;
	}

	const VariableDeclarationNode& var_decl = symbol->as<VariableDeclarationNode>();
	if (!var_decl.is_constexpr()) {
		return std::nullopt;
	}

	if (!var_decl.declaration().is_array()) {
		return size_t{1};
	}

	if (const auto array_size = var_decl.declaration().array_size(); array_size.has_value()) {
		const ASTNode& size_node = array_size.value();
		if (size_node.is<ExpressionNode>()) {
			const ExpressionNode& expr = size_node.as<ExpressionNode>();
			if (const auto* num_lit = std::get_if<NumericLiteralNode>(&expr)) {
				const auto literal_value = num_lit->value();
				if (const auto* ull_value = std::get_if<unsigned long long>(&literal_value)) {
					return static_cast<size_t>(*ull_value);
				}
			}
		}
	}

	const auto& initializer = var_decl.initializer();
	if (initializer.has_value() && initializer->is<InitializerListNode>()) {
		return initializer->as<InitializerListNode>().initializers().size();
	}

	return std::nullopt;
}

EvalResult make_checked_constexpr_pointer_result(
	std::string_view var_name,
	int64_t offset,
	EvaluationContext* context,
	const std::unordered_map<std::string_view, EvalResult>* bindings) {
	if (offset < 0) {
		return EvalResult::error("Pointer arithmetic produced negative offset " + std::to_string(offset) +
									 " in constant expression",
								 EvalErrorType::NotConstantExpression);
	}

	const auto upper_bound = try_get_constexpr_pointer_upper_bound(var_name, context, bindings);
	if (!upper_bound.has_value()) {
		return EvalResult::from_pointer(var_name, offset);
	}

	if (static_cast<uint64_t>(offset) > static_cast<uint64_t>(*upper_bound)) {
		return EvalResult::error("Pointer arithmetic produced offset " + std::to_string(offset) +
									 " outside the valid range [0, " + std::to_string(*upper_bound) +
									 "] for '" + std::string(var_name) + "' in constant expression",
								 EvalErrorType::NotConstantExpression);
	}

	return EvalResult::from_pointer(var_name, offset);
}

// Returns the result type after C++ usual arithmetic conversions, for use in
// binary arithmetic results.  Returns nullopt when either operand lacks an
// exact_type (caller should produce an untyped result in that case).
std::optional<TypeSpecifierNode> get_binary_arithmetic_result_type(
	const EvalResult& lhs, const EvalResult& rhs) {
	if (!lhs.exact_type.has_value() || !rhs.exact_type.has_value()) {
		return std::nullopt;
	}
	const TypeCategory result_type = get_common_type(lhs.exact_type->category(), rhs.exact_type->category());
	const SizeInBits result_bits{get_type_size_bits(result_type)};
	if (result_bits.is_set()) {
		return TypeSpecifierNode(result_type, TypeQualifier::None, result_bits, Token{}, CVQualifier::None);
	}
	return std::nullopt;
}

// Applies the declared-type width mask to an unsigned result so that, e.g.,
// unsigned int arithmetic wraps at 32 bits rather than 64.
// bits >= 64: the storage type is already 64-bit, so no masking is needed
// (and (1ULL << 64) would be undefined behaviour, so we must skip it).
unsigned long long apply_uint_type_mask(
	unsigned long long value, const std::optional<TypeSpecifierNode>& type_opt) {
	if (!type_opt.has_value()) {
		return value;
	}
	const int bits = type_opt->size_in_bits();
	// No masking required when the width is unknown, zero, or at the full
	// 64-bit storage width — and (1ULL << 64) would be undefined behaviour.
	if (bits <= 0 || bits >= 64) {
		return value;
	}
	return value & ((1ULL << bits) - 1);
}

// Keep alias-chain traversal bounded so malformed/self-referential bindings do
// not cause unbounded recursion/loops during constexpr evaluation.

const EvalResult* findLocalBinding(std::string_view name, EvaluationContext& context) {
	if (!context.local_bindings) {
		return nullptr;
	}

	auto it = context.local_bindings->find(name);
	return it == context.local_bindings->end() ? nullptr : &it->second;
}

EvalResult* findMutableLocalBinding(std::string_view name, EvaluationContext& context) {
	if (!context.local_bindings) {
		return nullptr;
	}

	auto it = context.local_bindings->find(name);
	return it == context.local_bindings->end() ? nullptr : &it->second;
}

const EvalResult* findBindingValue(
	std::string_view name,
	const std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context) {
	if (const EvalResult* local = findLocalBinding(name, context)) {
		return local;
	}

	auto it = bindings.find(name);
	return it == bindings.end() ? nullptr : &it->second;
}


const EvalResult* resolveReadThroughReferenceAlias(
	const EvalResult& bound_value,
	const std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context) {
	const EvalResult* current = &bound_value;
	for (size_t depth = 0; depth < kMaxReferenceAliasChainDepth; ++depth) {
		if (!isReferenceAliasBinding(*current)) {
			return current;
		}
		std::string_view pointed_name = StringTable::getStringView(current->pointer_to_var);
		const EvalResult* pointed_value = findBindingValue(pointed_name, bindings, context);
		if (!pointed_value) {
			return nullptr;
		}
		current = pointed_value;
	}
	return nullptr;
}

bool resolveMutableReferenceAliasTarget(
	EvalResult*& target_binding,
	std::string_view& target_name,
	std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context,
	std::string_view diagnostic_name,
	std::optional<EvalResult>& resolve_error) {
	for (size_t depth = 0; depth < kMaxReferenceAliasChainDepth; ++depth) {
		if (!isReferenceAliasBinding(*target_binding)) {
			return true;
		}
		std::string_view pointed_name = StringTable::getStringView(target_binding->pointer_to_var);
		EvalResult* pointed_binding = findMutableBindingValue(pointed_name, bindings, context);
		if (!pointed_binding) {
			resolve_error = EvalResult::error(
				"Dangling reference binding in constant expression: " + std::string(diagnostic_name));
			return false;
		}
		target_binding = pointed_binding;
		target_name = pointed_name;
	}
	resolve_error = EvalResult::error(
		"Reference alias chain too deep in constant expression: " + std::string(diagnostic_name));
	return false;
}

EvalResult* findMutableBindingValue(
	std::string_view name,
	std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context) {
	if (EvalResult* local = findMutableLocalBinding(name, context)) {
		return local;
	}

	auto it = bindings.find(name);
	return it == bindings.end() ? nullptr : &it->second;
}

void refreshPointerSnapshotsForBindingInMap(
	std::unordered_map<std::string_view, EvalResult>& binding_map,
	std::string_view target_name,
	const EvalResult& target_value) {
	for (auto& entry : binding_map) {
		EvalResult& binding_value = entry.second;
		if (!binding_value.pointer_to_var.isValid()) {
			continue;
		}
		if (StringTable::getStringView(binding_value.pointer_to_var) != target_name) {
			continue;
		}

		if (target_value.is_array) {
			binding_value.pointer_value_snapshot.clear();
			if (!target_value.array_elements.empty()) {
				binding_value.pointer_value_snapshot = target_value.array_elements;
			} else {
				binding_value.pointer_value_snapshot.reserve(target_value.array_values.size());
				for (int64_t element_value : target_value.array_values) {
					binding_value.pointer_value_snapshot.push_back(EvalResult::from_int(element_value));
				}
			}
			continue;
		}

		binding_value.pointer_value_snapshot = {target_value};
	}
}

void refreshPointerSnapshotsForBinding(
	std::string_view target_name,
	const EvalResult& target_value,
	std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context) {
	if (context.local_bindings && context.local_bindings != &bindings) {
		refreshPointerSnapshotsForBindingInMap(*context.local_bindings, target_name, target_value);
	}
	refreshPointerSnapshotsForBindingInMap(bindings, target_name, target_value);
}


std::optional<BoundWriteTarget> resolveBoundWriteTarget(
	const ASTNode& expr,
	std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context,
	EvalResult (*evaluate_index_expression)(
		const ASTNode&,
		const std::unordered_map<std::string_view, EvalResult>&,
		EvaluationContext&),
	std::optional<EvalResult>& resolve_error) {
	auto expandArrayElements = [](EvalResult& array_value) {
		if (!array_value.array_elements.empty() || array_value.array_values.empty()) {
			return;
		}
		array_value.array_elements.reserve(array_value.array_values.size());
		for (int64_t element_value : array_value.array_values) {
			array_value.array_elements.push_back(EvalResult::from_int(element_value));
		}
	};
	auto failNegativePointerOffset = [&]() -> std::optional<BoundWriteTarget> {
		resolve_error = EvalResult::error("Negative pointer offset in dereference");
		return std::nullopt;
	};
	auto failArrayIndexOutOfBounds = [&]() -> std::optional<BoundWriteTarget> {
		resolve_error = EvalResult::error("Array index out of bounds in constant expression");
		return std::nullopt;
	};
	auto failNonArrayOffset = [&](std::string_view var_name) -> std::optional<BoundWriteTarget> {
		resolve_error = EvalResult::error("Cannot dereference pointer with non-zero offset on non-array variable '" +
										  std::string(var_name) + "'");
		return std::nullopt;
	};
	auto tryResolveArrayPointerTarget = [&](EvalResult& array_value, std::string_view root_name, int64_t offset) -> std::optional<BoundWriteTarget> {
		if (offset < 0) {
			return failNegativePointerOffset();
		}
		expandArrayElements(array_value);
		size_t index = static_cast<size_t>(offset);
		if (index >= array_value.array_elements.size()) {
			return failArrayIndexOutOfBounds();
		}
		return BoundWriteTarget{&array_value.array_elements[index], root_name};
	};

	if (const IdentifierNode* identifier = tryGetIdentifier(expr)) {
		if (EvalResult* binding = findMutableBindingValue(identifier->name(), bindings, context)) {
			EvalResult* target_binding = binding;
			std::string_view target_name = identifier->name();
			if (!resolveMutableReferenceAliasTarget(
					target_binding,
					target_name,
					bindings,
					context,
					identifier->name(),
					resolve_error)) {
				return std::nullopt;
			}
			return BoundWriteTarget{target_binding, target_name};
		}
		return std::nullopt;
	}

	if (const auto* unary_op = tryGetNode<UnaryOperatorNode>(expr)) {
		if (unary_op->op() != "*") {
			return std::nullopt;
		}

		EvalResult pointer_result = evaluate_index_expression(unary_op->get_operand(), bindings, context);
		if (!pointer_result.success()) {
			resolve_error = pointer_result;
			return std::nullopt;
		}
		if (!pointer_result.pointer_to_var.isValid()) {
			return std::nullopt;
		}

		if (!context.constexpr_heap.empty()) {
			StringHandle heap_key = pointer_result.pointer_to_var;
			auto heap_it = context.constexpr_heap.find(heap_key);
			if (heap_it != context.constexpr_heap.end()) {
				if (heap_it->second.freed) {
					resolve_error = EvalResult::error("Use after free in constant expression");
					return std::nullopt;
				}

				EvalResult& heap_value = heap_it->second.value;
				if (heap_value.is_array) {
					return tryResolveArrayPointerTarget(heap_value, {}, pointer_result.pointer_offset);
				}
				if (pointer_result.pointer_offset != 0) {
					return failNonArrayOffset(StringTable::getStringView(pointer_result.pointer_to_var));
				}
				return BoundWriteTarget{&heap_value, {}};
			}
		}

		std::string_view pointed_name = StringTable::getStringView(pointer_result.pointer_to_var);
		if (EvalResult* binding = findMutableBindingValue(pointed_name, bindings, context)) {
			if (binding->is_array) {
				return tryResolveArrayPointerTarget(*binding, pointed_name, pointer_result.pointer_offset);
			}
			if (pointer_result.pointer_offset != 0) {
				return failNonArrayOffset(pointed_name);
			}
			return BoundWriteTarget{binding, pointed_name};
		}

		return std::nullopt;
	}

	if (const auto* member_access = tryGetNode<MemberAccessNode>(expr)) {
		// Arrow access on 'this' (this->member) is equivalent to dot access on 'this'
		// because 'this' is always a pointer to the current object.
		if (member_access->is_arrow()) {
			const IdentifierNode* object_id = tryGetIdentifier(member_access->object());
			if (object_id && object_id->name() == "this") {
				if (EvalResult* binding = findMutableBindingValue(member_access->member_name(), bindings, context)) {
					return BoundWriteTarget{binding, member_access->member_name()};
				}
			}

			EvalResult pointer_result = evaluate_index_expression(member_access->object(), bindings, context);
			if (!pointer_result.success()) {
				resolve_error = pointer_result;
				return std::nullopt;
			}
			if (!pointer_result.pointer_to_var.isValid() || pointer_result.pointer_offset != 0) {
				return std::nullopt;
			}

			std::string_view pointed_name = StringTable::getStringView(pointer_result.pointer_to_var);
			if (EvalResult* binding = findMutableBindingValue(pointed_name, bindings, context)) {
				auto member_it = binding->object_member_bindings.find(member_access->member_name());
				if (member_it != binding->object_member_bindings.end()) {
					return BoundWriteTarget{&member_it->second, pointed_name};
				}
			}
			return std::nullopt;
		}

		if (const IdentifierNode* object_id = tryGetIdentifier(member_access->object());
			object_id && object_id->name() == "this") {
			if (EvalResult* binding = findMutableBindingValue(member_access->member_name(), bindings, context)) {
				return BoundWriteTarget{binding, member_access->member_name()};
			}
			return std::nullopt;
		}

		std::optional<BoundWriteTarget> base_target = resolveBoundWriteTarget(
			member_access->object(), bindings, context, evaluate_index_expression, resolve_error);
		if (!base_target.has_value() || base_target->slot == nullptr) {
			return std::nullopt;
		}

		auto member_it = base_target->slot->object_member_bindings.find(member_access->member_name());
		if (member_it == base_target->slot->object_member_bindings.end()) {
			return std::nullopt;
		}
		return BoundWriteTarget{&member_it->second, base_target->root_name};
	}

	if (const auto* subscript = tryGetNode<ArraySubscriptNode>(expr)) {
		EvalResult array_result = evaluate_index_expression(subscript->array_expr(), bindings, context);
		if (!array_result.success()) {
			resolve_error = array_result;
			return std::nullopt;
		}

		EvalResult index_result = evaluate_index_expression(subscript->index_expr(), bindings, context);
		if (!index_result.success()) {
			resolve_error = index_result;
			return std::nullopt;
		}

		long long index = index_result.as_int();
		if (array_result.pointer_to_var.isValid()) {
			long long final_index;
			// Overflow-safe pointer offset + index addition
			if ((index > 0 && array_result.pointer_offset > LLONG_MAX - index) ||
				(index < 0 && array_result.pointer_offset < LLONG_MIN - index)) {
				resolve_error = EvalResult::error("Signed integer overflow in constant expression", EvalErrorType::NotConstantExpression);
				return std::nullopt;
			}
			final_index = array_result.pointer_offset + index;
			if (final_index < 0) {
				resolve_error = EvalResult::error("Array index out of bounds while resolving constexpr lvalue");
				return std::nullopt;
			}

			StringHandle pointer_target = array_result.pointer_to_var;
			auto heap_it = context.constexpr_heap.find(pointer_target);
			if (heap_it != context.constexpr_heap.end()) {
				if (heap_it->second.freed) {
					resolve_error = EvalResult::error("Use after free in constant expression");
					return std::nullopt;
				}
				EvalResult& heap_value = heap_it->second.value;
				if (!heap_value.is_array) {
					// C++ allows p[0] on a pointer to a single object.
					if (final_index != 0) {
						resolve_error = EvalResult::error("Cannot dereference pointer with non-zero offset on non-array variable");
						return std::nullopt;
					}
					return BoundWriteTarget{&heap_value, {}};
				}
				expandArrayElements(heap_value);
				size_t element_index = static_cast<size_t>(final_index);
				if (element_index >= heap_value.array_elements.size()) {
					return failArrayIndexOutOfBounds();
				}
				return BoundWriteTarget{&heap_value.array_elements[element_index], {}};
			}

			std::string_view pointed_name = StringTable::getStringView(pointer_target);
			if (EvalResult* pointed_binding = findMutableBindingValue(pointed_name, bindings, context)) {
				if (!pointed_binding->is_array) {
					// C++ allows p[0] on a pointer to a single object.
					if (final_index != 0) {
						resolve_error = EvalResult::error("Cannot dereference pointer with non-zero offset on non-array variable");
						return std::nullopt;
					}
					return BoundWriteTarget{pointed_binding, pointed_name};
				}
				expandArrayElements(*pointed_binding);
				size_t element_index = static_cast<size_t>(final_index);
				if (element_index >= pointed_binding->array_elements.size()) {
					return failArrayIndexOutOfBounds();
				}
				return BoundWriteTarget{&pointed_binding->array_elements[element_index], pointed_name};
			}
		}

		std::optional<BoundWriteTarget> base_target = resolveBoundWriteTarget(
			subscript->array_expr(), bindings, context, evaluate_index_expression, resolve_error);
		if (!base_target.has_value() || base_target->slot == nullptr || !base_target->slot->is_array) {
			if (!resolve_error.has_value() && base_target.has_value() && base_target->slot != nullptr && !base_target->slot->is_array) {
				resolve_error = EvalResult::error("Subscript operator applied to non-array type in constant expression");
			}
			return std::nullopt;
		}

		expandArrayElements(*base_target->slot);
		if (index < 0 || static_cast<size_t>(index) >= base_target->slot->array_elements.size()) {
			resolve_error = EvalResult::error("Array index out of bounds while resolving constexpr lvalue");
			return std::nullopt;
		}

		return BoundWriteTarget{
			&base_target->slot->array_elements[static_cast<size_t>(index)],
			base_target->root_name};
	}

	return std::nullopt;
}

	// Extract the variable/member name for the address-of + array-subscript pattern.
	// Handles:  &data[i]       → "data" (plain identifier)
	//           &this->data[i] → "data" (member access via this)
	// Returns empty string_view when the pattern is not recognised.
std::string_view getArrayNameForAddressOf(const ASTNode& array_expr) {
	std::string_view name = getIdentifierNameFromAstNode(array_expr);
	if (!name.empty())
		return name;
		// Tolerate the `this->member` representation used inside member function bodies.
	if (const auto* ma = tryGetNode<MemberAccessNode>(array_expr)) {
		if (const IdentifierNode* obj_id = tryGetIdentifier(ma->object())) {
			if (obj_id->name() == "this")
				return ma->member_name();
		}
	}
	return {};
}

// Create a zero-initialized EvalResult for the given dimensions and element type.
// When dims is empty, returns a type-correct scalar zero (0.0 for float, 0u for unsigned, 0 for signed).
// For non-empty dims, returns a nested is_array EvalResult of the appropriate depth.
EvalResult make_zero_array_for_dims(std::span<const size_t> dims, TypeCategory element_type) {
	if (dims.empty()) {
		if (isFloatingPointType(element_type)) {
			return EvalResult::from_double(0.0);
		}
		if (isUnsignedIntegralType(element_type)) {
			return EvalResult::from_uint(0ULL);
		}
		return EvalResult::from_int(0LL);
	}
	EvalResult result;
	result.is_array = true;
	result.array_elements.resize(dims[0]);
	std::vector<size_t> inner_dims(dims.begin() + 1, dims.end());
	for (auto& elem : result.array_elements) {
		elem = make_zero_array_for_dims(inner_dims, element_type);
	}
	return result;
}


EvalResult materializeArrayInitializer(
	TypeIndex type_index,
	std::span<const size_t> array_dimensions,
	const InitializerListNode& init_list,
	ConstExpr::EvaluationContext& context) {
	if (array_dimensions.size() > 1) {
		return ConstExpr::Evaluator::materialize_array_value_with_spec(
			makeArrayTypeSpec(type_index, array_dimensions),
			init_list,
			context,
			nullptr);
	}
	return ConstExpr::Evaluator::materialize_array_value(type_index, init_list, context, nullptr);
}

std::optional<EvalResult> tryMaterializeMultidimArrayRow(
	const TypeSpecifierNode* type_spec,
	const InitializerListNode& init_list,
	size_t index,
	ConstExpr::EvaluationContext& context) {
	if (!type_spec || type_spec->array_dimension_count() <= 1) {
		return std::nullopt;
	}
	EvalResult materialized = ConstExpr::Evaluator::materialize_array_value_with_spec(*type_spec, init_list, context, nullptr);
	if (!materialized.success()) {
		return materialized;
	}
	if (index >= materialized.array_elements.size()) {
		return EvalResult::error("Array index " + std::to_string(index) + " out of bounds (size " + std::to_string(materialized.array_elements.size()) + ")");
	}
	return materialized.array_elements[index];
}

EvalResult materialize_member_initializer_value(
	const StructMember& member_info,
	const ASTNode& initializer,
	EvaluationContext& context,
	const std::unordered_map<std::string_view, EvalResult>* evaluation_bindings,
	bool enforce_list_narrowing) {
	if (initializer.is<InitializerListNode>()) {
		const InitializerListNode& init_list = initializer.as<InitializerListNode>();

		if (member_info.is_array) {
			return Evaluator::materialize_array_value(member_info.type_index, init_list, context, evaluation_bindings);
		}

		if (is_struct_type(member_info.type_index.category())) {
			if (const TypeInfo* member_type_info = tryGetTypeInfo(member_info.type_index);
				const StructTypeInfo* member_struct_info = member_type_info ? member_type_info->getStructInfo() : nullptr) {
				ChunkedVector<ASTNode> ctor_args;
				for (const auto& arg : init_list.initializers()) {
					ctor_args.push_back(arg);
				}
				if (auto ctor_result = Evaluator::try_materialize_struct_from_ctor_args(
						member_struct_info,
						member_info.type_index,
						ctor_args,
						context,
						false,
						evaluation_bindings,
						nullptr,
						false)) {
					return std::move(*ctor_result);
				}
				return Evaluator::materialize_aggregate_object_value(
					member_struct_info,
					member_info.type_index,
					init_list,
					context,
					evaluation_bindings);
			}
		}
	}

	EvalResult value = Evaluator::evaluate(initializer, context);
	if (!value.success()) {
		return value;
	}
	return applyAggregateMemberScalarInitialization(
		member_info,
		std::move(value),
		enforce_list_narrowing);
}

} // namespace ConstExpr