#include "Parser.h"
#include "ConstExprEvaluator.h"
#include "NameMangling.h"
#include "OverloadResolution.h"
#include "TypeTraitEvaluator.h"
#include <limits>

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

StringHandle getMemberTemplateOwnerName(StringHandle qualified_lookup_name, const ASTNode* template_node) {
	if (qualified_lookup_name.isValid()) {
		std::string_view qualified_name = qualified_lookup_name.view();
		if (size_t scope_pos = qualified_name.rfind("::");
			scope_pos != std::string_view::npos) {
			return StringTable::getOrInternStringHandle(
				qualified_name.substr(0, scope_pos));
		}
	}

	if (template_node != nullptr &&
		template_node->is<TemplateFunctionDeclarationNode>()) {
		return StringTable::getOrInternStringHandle(
			template_node
				->as<TemplateFunctionDeclarationNode>()
				.function_decl_node()
				.parent_struct_name());
	}

	return {};
}
}

bool Parser::tryAppendMemberDefaultTemplateArg(
	const TemplateParameterNode& param,
	const InlineVector<TemplateParameterNode, 4>& template_params,
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
	combined_template_params.reserve((outer_binding->params.empty() ? outer_binding->param_names.size() : outer_binding->params.size()) + template_params.size());
	combined_template_args.reserve((outer_binding->all_args.empty() ? outer_binding->param_args.size() : outer_binding->all_args.size()) + current_template_args.size());
	appendOuterBindingSubstitutionInputs(*outer_binding, combined_template_params, combined_template_args);

	for (const auto& template_param_node : template_params) {
		combined_template_params.push_back(ASTNode::emplace_node<TemplateParameterNode>(template_param_node));
	}
	for (const auto& current_arg : current_template_args) {
		combined_template_args.push_back(current_arg);
	}
	TemplateEnvironment outer_environment;
	TemplateEnvironment combined_environment;
	if (outer_binding != nullptr) {
		outer_environment = buildTemplateEnvironment(*outer_binding);
	}
	combined_environment = buildTemplateEnvironment(
		std::span<const TemplateParameterNode>(template_params.data(), template_params.size()),
		std::span<const TemplateTypeArg>(current_template_args.data(), current_template_args.size()),
		outer_binding != nullptr ? &outer_environment : nullptr);
	const TemplateSubstitutionFailurePolicy failure_policy = currentTemplateSubstitutionFailurePolicy();

	InlineVector<TemplateParameterNode, 4> typed_combined_params;
	typed_combined_params.reserve(combined_template_params.size());
	for (const ASTNode& param_node : combined_template_params) {
		if (const TemplateParameterNode* typed_param = tryGetTemplateParameterNode(param_node);
			typed_param != nullptr) {
			typed_combined_params.push_back(*typed_param);
		}
	}

	ASTNode substituted_default = substituteTemplateParameters(
		param.default_value(),
		typed_combined_params,
		combined_template_args);
	if (param.kind() == TemplateParameterKind::Type && substituted_default.is<TypeSpecifierNode>()) {
		current_template_args.push_back(TemplateTypeArg(substituted_default.as<TypeSpecifierNode>()));
		return true;
	}
	if (param.kind() == TemplateParameterKind::NonType && substituted_default.is<ExpressionNode>()) {
		ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
		eval_ctx.parser = this;
		eval_ctx.sema = getActiveSemanticAnalysis();
		eval_ctx.template_environment = combined_environment;
		auto eval_sub_map = buildSubstitutionParamMap(combined_environment);
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
		auto eval_result = ConstExpr::Evaluator::evaluate(substituted_default, eval_ctx);
		if (eval_result.success()) {
			current_template_args.push_back(templateTypeArgFromEvalResult(eval_result));
			return true;
		}
		if (eval_result.error_type == ConstExpr::EvalErrorType::TemplateDependentExpression &&
			failure_policy == TemplateSubstitutionFailurePolicy::ShapeOnly &&
			param.has_type()) {
			TemplateTypeArg dependent_default = TemplateTypeArg::makeDependentValue(
				param.nameHandle(),
				param.type_specifier_node().type());
			dependent_default.dependent_expr = substituted_default;
			current_template_args.push_back(std::move(dependent_default));
			return true;
		}
		if (failure_policy == TemplateSubstitutionFailurePolicy::HardUse) {
			throw CompileError("Failed to evaluate member template default argument for '" + std::string(param.name()) + "': " + eval_result.error_message);
		}
	}
	return false;
}

TemplateNameLookupRequest Parser::buildMemberFunctionTemplateLookupRequest(
	StringHandle owner_name,
	StringHandle member_name,
	bool is_dependent) const {
	TemplateNameLookupRequest request;
	request.name = member_name;
	request.owner_name = owner_name;
	request.lookup_kind = TemplateNameLookupKind::Member;
	request.is_dependent = is_dependent;
	request.include_base_classes = true;
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

std::vector<TemplateNameLookupCandidate> Parser::lookupMemberFunctionTemplateCandidatesForInstantiation(
	std::string_view struct_name,
	std::string_view member_name) {
	std::vector<TemplateNameLookupCandidate> candidates;
	std::unordered_set<const void*> seen_declarations;
	const StringHandle member_name_handle = StringTable::getOrInternStringHandle(member_name);
	const StringHandle requested_owner = StringTable::getOrInternStringHandle(struct_name);

	auto append_candidates = [&](const TemplateNameLookupResult& lookup_result) {
		for (const TemplateNameLookupCandidate& candidate : lookup_result.candidates) {
			if (candidate.identity.kind != TemplateDeclarationKind::FunctionTemplate) {
				continue;
			}
			const void* declaration_address = candidate.declaration.raw_pointer();
			if (declaration_address == nullptr ||
				!seen_declarations.insert(declaration_address).second) {
				continue;
			}
			candidates.push_back(candidate);
		}
	};

	TemplateNameLookupRequest direct_request = buildMemberFunctionTemplateLookupRequest(
		requested_owner,
		member_name_handle,
		false);
	append_candidates(gTemplateRegistry.lookupTemplateName(direct_request));

	if (candidates.empty()) {
		if (auto lookup_owner = getTemplateLookupOwnerName(struct_name)) {
			if (*lookup_owner != requested_owner) {
				TemplateNameLookupRequest base_request = buildMemberFunctionTemplateLookupRequest(
					*lookup_owner,
					member_name_handle,
					false);
				append_candidates(gTemplateRegistry.lookupTemplateName(base_request));
				if (!candidates.empty()) {
					FLASH_LOG(Templates, Debug, "Found base template class lookup: ", base_request.owner_name.view());
				}
			}
		}
	}

	if (candidates.empty()) {
		auto find_inherited_owner = [&](const auto& self, StringHandle struct_name_handle, int depth) -> StringHandle {
			constexpr int kMaxInheritanceDepth = 100;
			if (depth > kMaxInheritanceDepth) {
				return {};
			}

			TemplateNameLookupRequest request = buildMemberFunctionTemplateLookupRequest(
				struct_name_handle,
				member_name_handle,
				false);
			TemplateNameLookupResult lookup_result =
				gTemplateRegistry.lookupTemplateName(request);
			if (lookup_result.hasFunctionTemplate()) {
				return struct_name_handle;
			}

			auto struct_it = getTypesByNameMap().find(struct_name_handle);
			if (struct_it == getTypesByNameMap().end()) {
				return {};
			}

			const TypeInfo* struct_type_info = struct_it->second;
			if (!struct_type_info->struct_info_) {
				if (const TypeInfo* underlying_type =
						tryGetTypeInfo(struct_type_info->type_index_);
					underlying_type != nullptr &&
					underlying_type != struct_type_info &&
					underlying_type->struct_info_) {
					return self(self, underlying_type->name(), depth + 1);
				}
				return {};
			}

			const StructTypeInfo* struct_info =
				struct_type_info->struct_info_.get();
			for (const auto& base_class : struct_info->base_classes) {
				if (base_class.is_deferred) {
					const TypeInfo* resolved = nullptr;
					if (base_class.type_index.is_valid()) {
						resolved = tryGetTypeInfo(base_class.type_index);
						if (resolved != nullptr && !resolved->struct_info_ &&
							resolved->type_index_.is_valid()) {
							const TypeInfo* chained_type =
								tryGetTypeInfo(resolved->type_index_);
							if (chained_type != nullptr &&
								chained_type->struct_info_) {
								resolved = chained_type;
							}
						}
					}
					if (resolved != nullptr && resolved->struct_info_) {
						StringHandle inherited_owner =
							self(self, resolved->name(), depth + 1);
						if (inherited_owner.isValid()) {
							return inherited_owner;
						}
					}
					continue;
				}

				StringHandle base_name_handle =
					StringTable::getOrInternStringHandle(base_class.name);
				StringHandle inherited_owner =
					self(self, base_name_handle, depth + 1);
				if (inherited_owner.isValid()) {
					return inherited_owner;
				}
			}

			return {};
		};

		StringHandle inherited_owner =
			find_inherited_owner(find_inherited_owner, requested_owner, 0);
		if (inherited_owner.isValid()) {
			FLASH_LOG_FORMAT(
				Templates,
				Debug,
				"Inherited member template lookup rebound '{}::{}' to owner '{}'",
				struct_name,
				member_name,
				StringTable::getStringView(inherited_owner));
			TemplateNameLookupRequest inherited_request =
				buildMemberFunctionTemplateLookupRequest(
					inherited_owner,
					member_name_handle,
					false);
			TemplateNameLookupResult inherited_lookup =
				gTemplateRegistry.lookupTemplateName(inherited_request);
			for (const TemplateNameLookupCandidate& inherited_candidate :
				 inherited_lookup.candidates) {
				if (inherited_candidate.identity.kind !=
					TemplateDeclarationKind::FunctionTemplate) {
					continue;
				}

				const void* declaration_address =
					inherited_candidate.declaration.raw_pointer();
				if (declaration_address == nullptr ||
					!seen_declarations.insert(declaration_address).second) {
					continue;
				}

				TemplateNameLookupCandidate rebound_candidate =
					inherited_candidate;
				rebound_candidate.lookup_owner_name = requested_owner;
				rebound_candidate.declaring_owner_name = inherited_owner;
				rebound_candidate.inherited_depth = 1;
				candidates.push_back(rebound_candidate);
			}
		}
	}

	// NOTE: filterPhase1OrdinaryFunctionOverloads is intentionally NOT called here.
	// All members of a class are mutually visible within the class scope regardless
	// of their textual order -- in particular a member function template body can call
	// another member declared later in the same class body.  The phase1 cutoff filter
	// is only meaningful for ordinary (namespace-scope) unqualified lookup of free
	// functions, not for member lookup.  Applying it here would incorrectly drop
	// candidates for same-class members declared after the calling member's body.
	(void)0;

	return candidates;
}

std::optional<ASTNode> Parser::try_instantiate_member_function_template(
	std::string_view struct_name,
	std::string_view member_name,
	std::span<const TypeSpecifierNode> arg_types) {

	// Build the qualified template name
	StringBuilder qualified_name_sb;
	qualified_name_sb.append(struct_name).append("::").append(member_name);
	StringHandle qualified_name = StringTable::getOrInternStringHandle(qualified_name_sb);

	// Push a parser-level instantiation context for provenance tracking and backtraces.
	ScopedParserInstantiationContext inst_ctx_guard(*this, template_instantiation_mode_, qualified_name);

	// Route member template lookup through the semantic two-phase lookup request.
	std::vector<TemplateNameLookupCandidate> template_candidates =
		lookupMemberFunctionTemplateCandidatesForInstantiation(struct_name, member_name);

	if (template_candidates.empty()) {
		return std::nullopt;	 // Not a template
	}
	qualified_name = template_candidates.front().identity.lookup_name;

	if (arg_types.empty()) {
		return std::nullopt;	 // Can't deduce without arguments
	}

	// Attempt template-argument deduction for one overload candidate.
	// Returns the deduced arg list on success, or nullopt if deduction fails or the
	// template kind is not supported by this path.
	struct CandidateResult {
		const ASTNode* template_node = nullptr;
		StringHandle lookup_name{};
		std::vector<TemplateTypeArg> template_args;
		int specificity = 0;
		size_t overload_index = 0;  // assigned externally to template_candidates index; used to build
		                            // discriminated cache keys when multiple overloads exist
	};

	auto tryDeduceCandidate = [&](const TemplateNameLookupCandidate& lookup_candidate) -> std::optional<CandidateResult> {
		const ASTNode& template_node_cand = lookup_candidate.declaration;
		if (!template_node_cand.is<TemplateFunctionDeclarationNode>()) {
			return std::nullopt;
		}
		const TemplateFunctionDeclarationNode& template_func =
			template_node_cand.as<TemplateFunctionDeclarationNode>();
		const FunctionDeclarationNode& func_decl = template_func.function_decl_node();
		const auto& template_params = template_func.template_parameters();
		const OuterTemplateBinding* candidate_outer_binding =
			gTemplateRegistry.getOuterTemplateBinding(
				lookup_candidate.identity.lookup_name.view());

		if (!functionTemplateAcceptsCallArgumentCount(template_params, func_decl, arg_types.size())) {
			return std::nullopt;
		}

		// First run the shared shape-only deduction/viability service.  It does not
		// materialize the member template; it only computes the candidate's template
		// argument list.  If an older member-specific corner (notably an outer-binding
		// default) is not covered, keep the legacy local deduction path below as the
		// compatibility fallback.
		if (auto shared_deduction = deduceTemplateCandidateViability(
				template_params,
				func_decl,
				arg_types,
				0)) {
			return CandidateResult{
				&template_node_cand,
				lookup_candidate.identity.lookup_name,
				std::vector<TemplateTypeArg>(
					shared_deduction->template_args.begin(),
					shared_deduction->template_args.end()),
				computeFunctionTemplateSpecificity(template_func)};
		}

		// Build set of template parameter names for O(1) lookup.
		std::unordered_set<StringHandle, StringHash> tparam_names;
		for (const auto& tp : template_params) {
			tparam_names.insert(tp.nameHandle());
		}

		// Resolve the base name of a function-parameter type (possibly U, U*, U**, etc.).
		// Uses TypeInfo name first, then the token handle as a fallback.
		auto getBaseName = [&](const TypeSpecifierNode& fp_type) -> StringHandle {
			StringHandle base_name;
			if (const TypeInfo* ti = tryGetTypeInfo(fp_type.type_index())) {
				base_name = ti->name();
			}
			if (!base_name.isValid()) {
				base_name = fp_type.token().handle();
			}
			return base_name;
		};

		// For each function parameter whose base type is a template parameter, record how many
		// pointer levels the pattern adds on top of U (e.g. pick(U*) → depth["U"]=1).
		// This is used for two purposes:
		//   1. Viability: if the call arg has fewer pointer levels than the pattern requires, skip.
		//   2. Pointer-stripping deduction: deduced U = call_arg stripped of depth["U"] levels.
		std::unordered_map<StringHandle, size_t, StringHash> tparam_fp_pointer_depth;
		{
			const auto& func_params = func_decl.parameter_nodes();
			size_t call_idx = 0;
			for (const auto& fp_node : func_params) {
				if (!fp_node.is<DeclarationNode>()) {
					continue;
				}
				if (call_idx >= arg_types.size()) {
					break;
				}
				const auto& fp_decl = fp_node.as<DeclarationNode>();
				if (fp_decl.is_parameter_pack()) {
					break;  // variadic: the remaining args are consumed as a pack
				}
				const TypeSpecifierNode& fp_type = fp_decl.type_specifier_node();
				const TypeSpecifierNode& ca_type = arg_types[call_idx];

				StringHandle base_name = getBaseName(fp_type);
				if (base_name.isValid() && tparam_names.count(base_name) > 0) {
					// Viability check: a parameter pattern U*^N (e.g. U*, U**) requires at least
					// N pointer levels in the call argument.  Without this, pick(U*) would be
					// treated as viable for a plain-int argument, which is incorrect.
					if (fp_type.pointer_depth() > ca_type.pointer_depth()) {
						return std::nullopt;  // not enough pointer levels in the call argument
					}
					tparam_fp_pointer_depth[base_name] = fp_type.pointer_depth();
				}
				++call_idx;
			}
		}

		auto deduction_info = buildDeductionMapFromCallArgs(template_params, func_decl, arg_types, 0, nullptr);
		if (!deduction_info.has_value()) {
			return std::nullopt;
		}

		std::vector<TemplateTypeArg> template_args;
		size_t arg_index = 0;
		for (const auto& template_param_node : template_params) {
			const TemplateParameterNode& param = template_param_node;

			if (param.kind() == TemplateParameterKind::Template) {
				return std::nullopt;
			}
			if (param.kind() != TemplateParameterKind::Type) {
				return std::nullopt;
			}

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
						template_args.push_back(TemplateTypeArg::makeTypeSpecifier(ca_type));
					}
				}
				arg_index = end;
				continue;
			}

			// Use pre-deduced arg if available (from buildDeductionMapFromCallArgs).
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
				const TypeSpecifierNode& ca_type = arg_types[arg_index];
				// If the function parameter for this template param is U*^N (e.g. U*, U**),
				// strip N pointer levels from the call arg to deduce U.
				// e.g. pick(U*) called with int* → U = int (not int*).
				auto depth_it = tparam_fp_pointer_depth.find(param.nameHandle());
				size_t fp_ptr_depth =
					(depth_it != tparam_fp_pointer_depth.end()) ? depth_it->second : 0;
				if (fp_ptr_depth > 0 && ca_type.pointer_depth() >= fp_ptr_depth) {
					TypeSpecifierNode stripped = ca_type;
					stripped.limit_pointer_depth(ca_type.pointer_depth() - fp_ptr_depth);
					template_args.push_back(TemplateTypeArg::makeTypeSpecifier(stripped));
				} else {
					template_args.push_back(TemplateTypeArg::makeTypeSpecifier(ca_type));
				}
				++arg_index;
			} else {
				InlineVector<TemplateTypeArg, 4> default_args;
				for (const auto& existing_arg : template_args) {
					default_args.push_back(existing_arg);
				}
				if (!tryAppendMemberDefaultTemplateArg(
						param, template_params, candidate_outer_binding, default_args)) {
					return std::nullopt;
				}
				template_args.push_back(default_args.back());
			}
		}

		return CandidateResult{
			&template_node_cand,
			lookup_candidate.identity.lookup_name,
			std::move(template_args),
			computeFunctionTemplateSpecificity(template_func)};
	};

	// Collect all viable candidates in registry order, then pick the most specific one.
	// When multiple candidates have the same specificity, the first viable one wins
	// (preserves the pre-partial-ordering behavior and avoids spurious ambiguity errors).
	size_t best_idx = std::numeric_limits<size_t>::max();
	int best_specificity = -1;
	std::vector<CandidateResult> viable;
	for (size_t i = 0; i < template_candidates.size(); ++i) {
		auto candidate = tryDeduceCandidate(template_candidates[i]);
		if (!candidate.has_value()) {
			continue;
		}
		candidate->overload_index = i;
		if (candidate->specificity > best_specificity) {
			best_specificity = candidate->specificity;
			best_idx = viable.size();
		}
		viable.push_back(std::move(*candidate));
	}

	if (best_idx == std::numeric_limits<size_t>::max()) {
		return std::nullopt;
	}

	if (viable.size() > 1) {
		std::vector<ASTNode> shape_overloads;
		std::vector<size_t> shape_candidate_indices;
		shape_overloads.reserve(viable.size());
		shape_candidate_indices.reserve(viable.size());
		for (size_t i = 0; i < viable.size(); ++i) {
			const CandidateResult& candidate = viable[i];
			StringHandle shape_key_qualified_name = candidate.lookup_name;
			if (template_candidates.size() > 1) {
				StringBuilder discriminated_sb;
				discriminated_sb.append(candidate.lookup_name.view())
					.append("$ol")
					.append(static_cast<uint64_t>(candidate.overload_index));
				shape_key_qualified_name = StringTable::getOrInternStringHandle(discriminated_sb);
			}
			auto shape_key = FlashCpp::makeInstantiationKey(shape_key_qualified_name, candidate.template_args);
			if (auto existing_inst = gTemplateRegistry.getInstantiation(shape_key);
				existing_inst.has_value() && existing_inst->is<FunctionDeclarationNode>()) {
				shape_overloads.push_back(*existing_inst);
				shape_candidate_indices.push_back(i);
				continue;
			}
			auto shape_node = instantiate_member_function_template_core(
				StringTable::getStringView(
					getMemberTemplateOwnerName(
						candidate.lookup_name,
						candidate.template_node)),
				member_name,
				candidate.lookup_name,
				candidate.lookup_name,
				*candidate.template_node,
				candidate.template_args,
				shape_key,
				arg_types,
				false);
			if (shape_node.has_value() && shape_node->is<FunctionDeclarationNode>()) {
				shape_overloads.push_back(*shape_node);
				shape_candidate_indices.push_back(i);
			}
		}
		if (shape_overloads.size() > 1) {
			auto resolution = resolve_overload(shape_overloads, arg_types);
			if (resolution.has_match &&
				!resolution.is_ambiguous &&
				resolution.selected_overload != nullptr) {
				for (size_t i = 0; i < shape_overloads.size(); ++i) {
					if (resolution.selected_overload == &shape_overloads[i]) {
						size_t selected_idx = shape_candidate_indices[i];
						if (viable[selected_idx].specificity >= best_specificity) {
							best_idx = selected_idx;
						}
						break;
					}
				}
			}
		}
	}

	const CandidateResult& best = viable[best_idx];
	const StringHandle best_owner_name =
		getMemberTemplateOwnerName(best.lookup_name, best.template_node);

	// Build the instantiation key.
	// When there are multiple overloads with the same name, two different templates can deduce
	// to identical template args (e.g. pick(U)[U=int] and pick(U*)[U=int] both produce <int>).
	// To avoid a cache collision where the wrong function body is returned for the second
	// caller, we discriminate the key by appending the overload index when there is more
	// than one overload registered under this name.
	StringHandle key_qualified_name = best.lookup_name;
	if (template_candidates.size() > 1) {
		StringBuilder discriminated_sb;
		discriminated_sb.append(best.lookup_name.view())
			.append("$ol")
			.append(static_cast<uint64_t>(best.overload_index));
		key_qualified_name = StringTable::getOrInternStringHandle(discriminated_sb);
	}
	auto key = FlashCpp::makeInstantiationKey(key_qualified_name, best.template_args);

	auto existing_inst = gTemplateRegistry.getInstantiation(key);
	if (existing_inst.has_value()) {
		return *existing_inst;  // Return existing instantiation
	}

	return instantiate_member_function_template_core(
		StringTable::getStringView(
			best_owner_name.isValid()
				? best_owner_name
				: StringTable::getOrInternStringHandle(struct_name)),
		member_name,
		best.lookup_name,
		best.lookup_name,
		*best.template_node, best.template_args, key, arg_types, true);
}

std::optional<ASTNode> Parser::try_instantiate_constructor_template(
	StringHandle instantiated_struct_name,
	const ConstructorDeclarationNode& ctor_decl,
	std::span<const TypeSpecifierNode> arg_types) {
	const auto& template_params = ctor_decl.template_parameters();
	if (template_params.empty()) {
		return std::nullopt;
	}

	auto deduction_candidate = deduceTemplateCandidateViability(
		template_params,
		ctor_decl,
		arg_types,
		0);
	if (!deduction_candidate.has_value()) {
		return std::nullopt;
	}
	InlineVector<TemplateTypeArg, 4> ctor_template_args =
		std::move(deduction_candidate->template_args);

	LazyMemberFunctionInfo lazy_info;
	lazy_info.identity.original_member_node = emplace_node<ConstructorDeclarationNode>(ctor_decl);
	lazy_info.identity.template_owner_name = instantiated_struct_name;
	lazy_info.identity.instantiated_owner_name = instantiated_struct_name;
	lazy_info.identity.original_lookup_name = ctor_decl.name();
	lazy_info.identity.kind = DeferredMemberIdentity::Kind::Constructor;
	lazy_info.identity.is_const_method = false;

	InlineVector<StringHandle, 4> outer_param_names;
	InlineVector<TypeInfo::TemplateArgInfo, 4> outer_args;
	populateTemplateEnvironmentLegacyViews(
		ctor_decl.outer_template_environment_snapshot(),
		outer_param_names,
		outer_args);
	const TemplateEnvironmentSnapshot* outer_parent_snapshot =
		ctor_decl.has_outer_template_bindings()
			? &ctor_decl.outer_template_environment_snapshot()
			: nullptr;
	lazy_info.outer_template_environment_snapshot = buildTemplateEnvironmentSnapshotFromBindings(
		template_params,
		ctor_template_args,
		outer_parent_snapshot);
	for (StringHandle outer_name : outer_param_names) {
		Token outer_token(Token::Type::Identifier, StringTable::getStringView(outer_name), 0, 0, 0);
		lazy_info.template_params.push_back(TemplateParameterNode(outer_name, outer_token));
	}
	for (const auto& outer_arg : outer_args) {
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
	std::span<const TypeSpecifierNode> arg_types,
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
			const auto& param_type = ctor.parameter_nodes()[i].as<DeclarationNode>().type_specifier_node();
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
	std::span<const TemplateTypeArg> template_type_args) {

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
				const std::span<const int64_t> non_type_args = spec_func.non_type_template_args();
				inst_func_ref.set_non_type_template_args(std::vector<int64_t>(non_type_args.begin(), non_type_args.end()));
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

	// Route member template overload discovery through the semantic two-phase lookup request.
	std::vector<TemplateNameLookupCandidate> template_candidates =
		lookupMemberFunctionTemplateCandidatesForInstantiation(struct_name, member_name);

	if (template_candidates.empty()) {
		return std::nullopt;	 // Not a template
	}
	qualified_name = template_candidates.front().identity.lookup_name;

	// Loop over all overloads for SFINAE support
	for (const TemplateNameLookupCandidate& lookup_candidate : template_candidates) {
		const ASTNode& template_node = lookup_candidate.declaration;
		if (!template_node.is<TemplateFunctionDeclarationNode>()) {
			continue;  // Not a function template
		}

		const TemplateFunctionDeclarationNode& template_func = template_node.as<TemplateFunctionDeclarationNode>();
		const auto& template_params = template_func.template_parameters();
		const FunctionDeclarationNode& func_decl = template_func.function_decl_node();
		if (template_type_args.size() > template_params.size()) {
			continue;
		}

		InlineVector<TemplateTypeArg, 4> completed_template_args;
		const StringHandle candidate_qualified_name = lookup_candidate.identity.lookup_name;
		for (const auto& arg : template_type_args) {
			completed_template_args.push_back(arg);
		}
		bool has_all_template_args = true;
		for (size_t i = completed_template_args.size(); i < template_params.size(); ++i) {
			const auto& template_param = template_params[i];
			if (!tryAppendMemberDefaultTemplateArg(
					template_param,
					template_params,
					gTemplateRegistry.getOuterTemplateBinding(candidate_qualified_name.view()),
					completed_template_args)) {
				has_all_template_args = false;
				break;
			}
		}
		if (!has_all_template_args) {
			continue;
		}
		const auto& template_args = completed_template_args;
		auto key = FlashCpp::makeInstantiationKey(candidate_qualified_name, template_args);

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
			ScopedParserInstantiationContext guard_instantiation_mode(*this, TemplateInstantiationMode::SoftProbe, StringHandle{});
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
			if (const OuterTemplateBinding* sfinae_outer_binding =
					gTemplateRegistry.getOuterTemplateBinding(candidate_qualified_name.view())) {
				registerOuterBindingInScope(*sfinae_outer_binding, sfinae_scope, &sfinae_type_map_);
			}

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
		std::span<const TypeSpecifierNode> call_arg_types =
			current_explicit_call_arg_types_ != nullptr
				? *current_explicit_call_arg_types_
				: empty_call_arg_types;
		const StringHandle candidate_owner_name =
			lookup_candidate.declaring_owner_name.isValid()
				? lookup_candidate.declaring_owner_name
				: struct_name_handle;
		FLASH_LOG_FORMAT(
			Templates,
			Debug,
			"Trying explicit member template instantiation via owner '{}', lookup '{}'",
			StringTable::getStringView(candidate_owner_name),
			candidate_qualified_name.view());
		auto result = instantiate_member_function_template_core(
			StringTable::getStringView(candidate_owner_name), member_name, candidate_qualified_name, candidate_qualified_name, template_node, template_args, key, call_arg_types, true);
		if (result.has_value()) {
			FLASH_LOG_FORMAT(
				Templates,
				Debug,
				"Explicit member template instantiation succeeded for '{}'",
				candidate_qualified_name.view());
			return result;
		}
	}

	return std::nullopt;
}

static TemplateParameterNode cloneNonVariadicTemplateParam(const TemplateParameterNode& param) {
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
}

void Parser::appendOuterBindingSubstitutionInputs(
	const OuterTemplateBinding& outer_binding,
	InlineVector<ASTNode, 4>& out_params,
	InlineVector<TemplateTypeArg, 4>& out_args) {
	if (!outer_binding.params.empty()) {
		for (const auto& outer_param : outer_binding.params) {
			out_params.push_back(outer_param);
		}
		for (const auto& outer_arg : outer_binding.all_args) {
			out_args.push_back(outer_arg);
		}
		return;
	}

	if (outer_binding.param_names.size() != outer_binding.param_args.size()) {
		throw InternalError("Outer template binding parameter state is inconsistent");
	}

	for (size_t i = 0; i < outer_binding.param_names.size(); ++i) {
		StringHandle outer_name = outer_binding.param_names[i];
		const TemplateTypeArg& outer_arg = outer_binding.param_args[i];
		Token outer_token(Token::Type::Identifier, StringTable::getStringView(outer_name), 0, 0, 0);
		if (outer_arg.is_value) {
			auto outer_type_node = emplace_node<TypeSpecifierNode>(
				outer_arg.type_index.withCategory(outer_arg.typeEnum()),
				get_type_size_bits(outer_arg.typeEnum()),
				outer_token,
				CVQualifier::None,
				ReferenceQualifier::None);
			out_params.push_back(
				emplace_node<TemplateParameterNode>(outer_name, outer_type_node.as<TypeSpecifierNode>(), outer_token));
		} else if (outer_arg.is_template_template_arg) {
			out_params.push_back(
				emplace_node<TemplateParameterNode>(outer_name, std::vector<ASTNode>{}, outer_token));
		} else {
			auto outer_param = emplace_node<TemplateParameterNode>(outer_name, outer_token);
			outer_param.as<TemplateParameterNode>().set_registered_type_index(
				outer_arg.type_index.withCategory(outer_arg.typeEnum()));
			out_params.push_back(outer_param);
		}
		out_args.push_back(outer_arg);
	}
}

bool Parser::buildSubstitutionForPackElement(
	StringHandle pack_param_name,
	size_t pack_element_offset,
	const std::unordered_set<StringHandle, StringHash, StringEqual>& dependent_pack_names,
	std::span<const TemplateParameterNode> template_params,
	std::span<const size_t> template_param_arg_starts,
	std::span<const size_t> template_param_arg_counts,
	std::span<const TemplateTypeArg> template_args,
	InlineVector<ASTNode, 4>& subst_params,
	InlineVector<TemplateTypeArg, 4>& subst_args) {
	std::optional<std::pair<size_t, size_t>> pack_binding;
	for (size_t i = 0; i < template_params.size(); ++i) {
		const TemplateParameterNode* tparam = &template_params[i];
		if (tparam->is_variadic() && tparam->nameHandle() == pack_param_name) {
			pack_binding = std::pair<size_t, size_t>{
				template_param_arg_starts[i],
				template_param_arg_counts[i]};
			break;
		}
	}
	if (!pack_binding.has_value() || pack_element_offset >= pack_binding->second) {
		return false;
	}
	for (size_t i = 0; i < template_params.size(); ++i) {
		const TemplateParameterNode* tparam = &template_params[i];
		if (tparam->is_variadic()) {
			const bool is_primary = (tparam->nameHandle() == pack_param_name);
			const bool is_co_pack = !is_primary &&
				dependent_pack_names.count(tparam->nameHandle());
			if (is_primary || is_co_pack) {
				size_t pack_index = template_param_arg_starts[i] + pack_element_offset;
				if (pack_index < template_args.size()) {
					subst_params.push_back(ASTNode::emplace_node<TemplateParameterNode>(
						cloneNonVariadicTemplateParam(*tparam)));
					subst_args.push_back(template_args[pack_index]);
				}
			}
			continue;
		}
		if (template_param_arg_starts[i] == SIZE_MAX || template_param_arg_starts[i] >= template_args.size()) {
			continue;
		}
		subst_params.push_back(ASTNode::emplace_node<TemplateParameterNode>(*tparam));
		subst_args.push_back(template_args[template_param_arg_starts[i]]);
	}
	return true;
}

ASTNode Parser::buildMaterializedParamType(
	const TypeSpecifierNode& original_param_type,
	const InlineVector<ASTNode, 4>& materialized_template_params,
	const InlineVector<TemplateTypeArg, 4>& materialized_template_args) {
	InlineVector<TemplateParameterNode, 4> typed_params;
	typed_params.reserve(materialized_template_params.size());
	for (const ASTNode& param_node : materialized_template_params) {
		if (const TemplateParameterNode* typed_param = tryGetTemplateParameterNode(param_node);
			typed_param != nullptr) {
			typed_params.push_back(*typed_param);
		}
	}

	TypeIndex substituted_type_index = substitute_template_parameter(
		original_param_type,
		typed_params,
		materialized_template_args);
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
}

std::optional<ASTNode> Parser::instantiate_member_function_template_core(
	std::string_view struct_name, std::string_view member_name,
	StringHandle requested_qualified_name,
	StringHandle qualified_name,
	const ASTNode& template_node,
	std::span<const TemplateTypeArg> template_args,
	const FlashCpp::TemplateInstantiationKey& key,
	std::span<const TypeSpecifierNode> call_arg_types,
	bool materialize_body) {

	// Depth guard: instantiating a member function template replays its body,
	// and body expressions can recursively trigger further member-function
	// template instantiations.  libstdc++ container headers (<string>, <vector>,
	// <map>) hit dozens of nested replays through iterator/alloc_traits SFINAE
	// chains before exhausting the thread stack.  Bail cleanly instead of
	// crashing on the guard page.
	static thread_local size_t s_member_inst_depth = 0;
	static thread_local bool s_member_inst_depth_warned = false;
	static constexpr size_t MAX_MEMBER_INST_DEPTH = 40;
	++s_member_inst_depth;
	struct DepthGuard {
		~DepthGuard() {
			if (--s_member_inst_depth == 0) {
				s_member_inst_depth_warned = false;
			}
		}
	} depth_guard;
	if (s_member_inst_depth > MAX_MEMBER_INST_DEPTH) {
		std::string_view failure_reason = StringBuilder()
			.append("Max member function template instantiation depth (")
			.append(static_cast<uint64_t>(MAX_MEMBER_INST_DEPTH))
			.append(") exceeded for '")
			.append(qualified_name)
			.append("'. Possible recursive template instantiation.")
			.commit();
		if (!s_member_inst_depth_warned) {
			FLASH_LOG(Templates, Error, failure_reason);
			s_member_inst_depth_warned = true;
		}
		return failTemplateInstantiation(failure_reason, &key, reinterpret_cast<uintptr_t>(&template_node));
	}

	const TemplateFunctionDeclarationNode& template_func = template_node.as<TemplateFunctionDeclarationNode>();
	const auto& template_params = template_func.template_parameters();
	const FunctionDeclarationNode& func_decl = template_func.function_decl_node();
	const OuterTemplateBinding* outer_binding =
		gTemplateRegistry.getOuterTemplateBinding(requested_qualified_name.view());
	InlineVector<TemplateTypeArg, 4> inline_template_args;
	inline_template_args.reserve(template_args.size());
	for (const TemplateTypeArg& template_arg : template_args) {
		inline_template_args.push_back(template_arg);
	}
	auto deduction_info = buildDeductionMapFromCallArgs(
		template_params,
		func_decl,
		call_arg_types,
		0,
		nullptr);

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
				const TemplateParameterNode& tparam = template_params[i];
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
	const TypeSpecifierNode& return_type_spec = orig_decl.type_specifier_node();
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
	TemplateEnvironment outer_default_environment;
	TemplateEnvironment default_environment;
	if (outer_binding) {
		outer_default_environment = buildTemplateEnvironment(*outer_binding);
	}
	default_environment = buildTemplateEnvironment(
		std::span<const TemplateParameterNode>(template_params.data(), template_params.size()),
		template_args,
		outer_binding ? &outer_default_environment : nullptr);
	auto default_sub_map = buildSubstitutionParamMap(default_environment);
	for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
		const auto& template_param = template_params[i];
		if (template_param.kind() == TemplateParameterKind::Type && !template_args[i].is_value) {
			auto type_it = getTypesByNameMap().find(template_param.nameHandle());
			if (type_it != getTypesByNameMap().end()) {
				default_type_sub_map[type_it->second->type_index_] = template_args[i];
			} else {
				default_type_sub_map[TypeIndex{getTypeInfoCount() + default_type_sub_map.size() + 1}] = template_args[i];
			}
		}
	}
	for (const auto& [param_name, default_arg] : default_sub_map.param_map) {
		if (default_arg.is_value) {
			default_nontype_sub_map[param_name] = default_arg.value;
			continue;
		}
		auto type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(param_name));
		if (type_it != getTypesByNameMap().end()) {
			default_type_sub_map[type_it->second->type_index_] = default_arg;
		}
	}

	// Save and reset pack_param_info_ so this instantiation can rebuild its local pack state.
	// ScopeGuard ensures restoration on every exit path, including early returns.
	auto saved_pack_param_info = std::move(pack_param_info_);
	ScopeGuard restore_pack_param_info([&]() {
		pack_param_info_ = std::move(saved_pack_param_info);
	});

	// Helper to extract the type name from a TypeSpecifierNode as a StringHandle.
	// Returns an invalid handle when the type is not a user-defined/alias/template type.
	auto getTypeName = [&](const TypeSpecifierNode& type_spec) -> StringHandle {
		if (type_spec.category() != TypeCategory::UserDefined &&
			type_spec.category() != TypeCategory::TypeAlias &&
			type_spec.category() != TypeCategory::Template) {
			return {};
		}
		if (type_spec.type_index().is_valid()) {
			if (const TypeInfo* ti = tryGetTypeInfo(type_spec.type_index())) {
				return ti->name();
			}
		}
		return type_spec.token().handle();
	};
	std::unordered_map<StringHandle, const TemplateParameterNode*, StringHash, StringEqual> tparam_nodes_by_name;
	for (const auto& template_param_node : template_params) {
		const auto& template_param = template_param_node;
		tparam_nodes_by_name.emplace(template_param.nameHandle(), &template_param);
	}
	InlineVector<size_t, 8> template_param_arg_starts;
	InlineVector<size_t, 8> template_param_arg_counts;
	template_param_arg_starts.reserve(template_params.size());
	template_param_arg_counts.reserve(template_params.size());
	for (size_t i = 0; i < template_params.size(); ++i) {
		template_param_arg_starts.push_back(SIZE_MAX);
		template_param_arg_counts.push_back(0);
	}
	{
		size_t template_arg_index = 0;
		for (size_t i = 0; i < template_params.size(); ++i) {
			const TemplateParameterNode& tparam = template_params[i];
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
					size_t required_after = countRequiredTemplateArgsAfter(
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

	// Populate template_param_pack_sizes_ so substituteTemplateParameters can resolve
	// sizeof...(Pack) correctly for member function templates, including unnamed
	// function parameter packs where pack_param_info_ cannot help. Saved/restored
	// around both the body reparse path and the direct AST-substitution fallback so
	// nested instantiations do not observe stale pack metadata.
	auto saved_template_pack_sizes = std::move(template_param_pack_sizes_);
	ScopeGuard restore_template_pack_sizes([&]() {
		template_param_pack_sizes_ = std::move(saved_template_pack_sizes);
	});
	template_param_pack_sizes_.clear();
	for (size_t pi = 0; pi < template_params.size(); ++pi) {
		const auto& tparam = template_params[pi];
		if (tparam.is_variadic() && template_param_arg_starts[pi] != SIZE_MAX) {
			template_param_pack_sizes_.emplace_back(tparam.nameHandle(), template_param_arg_counts[pi]);
		}
	}

	auto getPackParameterName = [&](const TypeSpecifierNode& type_spec,
								   StringHandle& primary_pack_name,
								   std::unordered_set<StringHandle, StringHash, StringEqual>& dependent_pack_names) {
		primary_pack_name = {};
		dependent_pack_names.clear();
		StringHandle type_name_handle = getTypeName(type_spec);
		if (type_name_handle.isValid() && tparam_nodes_by_name.count(type_name_handle)) {
			primary_pack_name = type_name_handle;
			dependent_pack_names.insert(type_name_handle);
		}
		collectDependentTemplateParamNamesFromType(
			type_spec.type_index(),
			tparam_nodes_by_name,
			primary_pack_name,
			dependent_pack_names);
		// Ensure primary_pack_name refers to a variadic parameter. If the first
		// dependent name found is non-variadic (e.g. T in Foo<T, Ts>...), scan
		// the full dependent set to find a variadic one.
		if (primary_pack_name.isValid()) {
			auto it = tparam_nodes_by_name.find(primary_pack_name);
			if (it != tparam_nodes_by_name.end() && !it->second->is_variadic()) {
				for (StringHandle dep : dependent_pack_names) {
					auto dep_it = tparam_nodes_by_name.find(dep);
					if (dep_it != tparam_nodes_by_name.end() && dep_it->second->is_variadic()) {
						primary_pack_name = dep;
						break;
					}
				}
			}
		}
	};
	auto getTemplateParamPackBinding = [&](StringHandle pack_param_name) -> std::optional<std::pair<size_t, size_t>> {
		for (size_t i = 0; i < template_params.size(); ++i) {
			const auto& tparam = template_params[i];
			if (tparam.is_variadic() && tparam.nameHandle() == pack_param_name) {
				return std::pair<size_t, size_t>{
					template_param_arg_starts[i],
					template_param_arg_counts[i]};
			}
		}
		return std::nullopt;
	};


	// Copy parameters while substituting template arguments and expanding variadic packs.
	size_t materialized_param_index = 0;
	for (const auto& param : func_decl.parameter_nodes()) {
		if (param.is<DeclarationNode>()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			const TypeSpecifierNode& param_type_spec = param_decl.type_specifier_node();

			// Expand variadic pack parameters (including wrapped nested pack element types).
			bool handled_as_pack = false;
			bool is_pack_param = param_decl.is_parameter_pack();

			// Also detect if type references a variadic template parameter (for cases where is_parameter_pack isn't set)
			StringHandle type_name_handle = getTypeName(param_type_spec);
			if (!is_pack_param && type_name_handle.isValid()) {
				for (size_t i = 0; i < template_params.size(); ++i) {
					const TemplateParameterNode& tparam = template_params[i];
					if (tparam.is_variadic() && tparam.nameHandle() == type_name_handle) {
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
				getPackParameterName(
					param_type_spec,
					primary_pack_name,
					dependent_pack_names);
				auto pack_binding = getTemplateParamPackBinding(primary_pack_name);
				if (pack_binding.has_value()) {
					std::string_view orig_name = param_decl.identifier_token().value();
					for (size_t pi = 0; pi < pack_binding->second; ++pi) {
						InlineVector<ASTNode, 4> subst_params;
						InlineVector<TemplateTypeArg, 4> subst_args;
						if (!buildSubstitutionForPackElement(
								primary_pack_name,
								pi,
								dependent_pack_names,
								std::span<const TemplateParameterNode>(template_params.data(), template_params.size()),
								template_param_arg_starts,
								template_param_arg_counts,
								template_args,
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
					pack_param_info_.push_back({orig_name, materialized_param_index, pack_binding->second});
					materialized_param_index += pack_binding->second;
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
				ExpressionSubstitutor substitutor(default_environment, *this);
				ASTNode substituted_default = substitutor.substitute(param_decl.default_value());
				if (substituted_default.is<ExpressionNode>() &&
					std::holds_alternative<ConstructorCallNode>(substituted_default.as<ExpressionNode>())) {
					substituted_default = substitute_template_params_in_expression(
						substituted_default, default_type_sub_map, default_nontype_sub_map, StringHandle{});
				}
				new_param_decl.as<DeclarationNode>().set_default_value(substituted_default);
			}
			new_func_ref.add_parameter_node(new_param_decl);
			++materialized_param_index;
		}
	}

	copy_function_properties(new_func_ref, func_decl);
	auto orig_body = func_decl.get_definition();
	if (!materialize_body) {
		compute_and_set_mangled_name(new_func_ref);
		return new_func_node;
	}

	// Check if the template has a body position stored
	if (!func_decl.has_template_body_position()) {
		if (orig_body.has_value()) {
			ASTNode substituted_body = substituteTemplateParameters(
				*orig_body,
				template_params,
				inline_template_args);
			if (func_decl.has_outer_template_bindings()) {
				InlineVector<StringHandle, 4> outer_param_names;
				InlineVector<TypeInfo::TemplateArgInfo, 4> outer_arg_infos;
				outer_param_names = func_decl.outer_template_param_names();
				outer_arg_infos = func_decl.outer_template_args();
				InlineVector<TemplateParameterNode, 4> typed_outer_params;
				typed_outer_params.reserve(outer_param_names.size());
				InlineVector<TemplateTypeArg, 4> outer_args;
				outer_args.reserve(outer_arg_infos.size());
				for (size_t i = 0; i < outer_param_names.size() && i < outer_arg_infos.size(); ++i) {
					Token outer_token(Token::Type::Identifier, StringTable::getStringView(outer_param_names[i]), 0, 0, 0);
					TemplateParameterNode outer_param(outer_param_names[i], outer_token);
					typed_outer_params.push_back(outer_param);
					outer_args.push_back(toTemplateTypeArg(outer_arg_infos[i]));
				}
				substituted_body = substituteTemplateParameters(
					substituted_body,
					typed_outer_params,
					outer_args);
			}
			new_func_ref.set_definition(substituted_body);
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
		param_names.push_back(tparam_node.nameHandle());
	}

	// Kind::Value and Kind::Template entries are intentionally skipped by registerTypeParamsInScope:
	// registering them would poison getTypesByNameMap() with Invalid/garbage TypeInfo entries.
	registerTypeParamsInScope(template_params, template_args, template_scope, false);

	// Also add outer template parameter bindings (e.g., T→int from class template)
	if (func_decl.has_outer_template_bindings()) {
		InlineVector<StringHandle, 4> outer_param_names = func_decl.outer_template_param_names();
		InlineVector<TypeInfo::TemplateArgInfo, 4> outer_arg_infos = func_decl.outer_template_args();
		InlineVector<TemplateTypeArg, 4> outer_args;
		outer_args.reserve(outer_arg_infos.size());
		for (const auto& outer_arg : outer_arg_infos) {
			outer_args.push_back(toTemplateTypeArg(outer_arg));
		}
		registerTypeParamsInScope(outer_param_names, outer_args, template_scope, true);
		FLASH_LOG(Templates, Debug, "Added ", outer_param_names.size(), " outer template param bindings for body parsing");
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
		TemplateEnvironment substitution_environment = buildTemplateEnvironment(
			template_params,
			template_args,
			outer_binding ? &outer_default_environment : nullptr);
		populateTemplateParamSubstitutions(template_param_substitutions_, substitution_environment);

		// Phase 1 two-phase lookup (C++20 [temp.res]/9): record the template body's
		// opening-brace line so non-dependent calls inside the body use definition-time
		// lookup rather than point-of-instantiation lookup.  This mirrors the identical
		// setup in try_instantiate_single_template (Parser_Templates_Inst_Deduction.cpp).
		//
		// Reset unconditionally so stale state from a previous instantiation cannot
		// leak into this one — even when body_position is invalid or has no saved token.
		phase1_cutoff_line_ = 0;
		phase1_cutoff_file_idx_ = SIZE_MAX;
		phase1_violation_token_.reset();
		{
			SaveHandle body_position = func_decl.template_body_position();
			if (body_position < saved_tokens_.size() && saved_tokens_[body_position].has_value()) {
				const SavedToken& saved_token = *saved_tokens_[body_position];
				phase1_cutoff_line_ = saved_token.current_token_.line();
				phase1_cutoff_file_idx_ = saved_token.current_token_.file_index();
			}
			// If body_position is invalid or has no saved token (e.g. the template was
			// defined in a context where tokens were not saved), phase1_cutoff_line_
			// remains 0, is_valid() will return false below, and the context will not
			// be installed -- parsing proceeds without phase-1 cutoff for this body.
		}
		TemplateDefinitionLookupContext definition_lookup_context;
		definition_lookup_context.definition_line = phase1_cutoff_line_;
		definition_lookup_context.definition_file_index = phase1_cutoff_file_idx_;
		definition_lookup_context.definition_namespace = gSymbolTable.get_current_namespace_handle();
		definition_lookup_context.current_instantiation_name =
			StringTable::getOrInternStringHandle(struct_name);
		const TemplateDefinitionLookupContext* previous_definition_lookup_context =
			current_template_definition_lookup_context_;
		current_template_definition_lookup_context_ = definition_lookup_context.is_valid()
			? &definition_lookup_context
			: nullptr;
		auto restore_definition_lookup_context = ScopeGuard([&]() {
			current_template_definition_lookup_context_ = previous_definition_lookup_context;
		});

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
					inline_template_args);
				new_func_ref.set_definition(substituted_body);
			}
		} // current_template_param_names_ restored here

		// Restore phase1 cutoff after body is parsed (same as try_instantiate_single_template).
		// Check for Phase 1 violations: a non-dependent name in the body that was only
		// declared after the template definition is ill-formed (C++20 [temp.res]/9).
		phase1_cutoff_line_ = 0;
		phase1_cutoff_file_idx_ = SIZE_MAX;
		if (phase1_violation_token_.has_value()) {
			auto tok = *phase1_violation_token_;
			phase1_violation_token_.reset();
			throw CompileError(
				std::string("non-dependent name '").append(tok.value()).append("' was not declared before the template definition (C++20 [temp.res]/9)"));
		}
	} // template_param_substitutions_ and definition_lookup_context restored here

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
	if (current_pos < saved_tokens_.size() && saved_tokens_[current_pos].has_value()) {
		saved_tokens_[current_pos]->ast_nodes_size_ = ast_nodes_.size();
	}

	if (new_func_ref.is_materialized()) {
		finalize_function_after_definition(new_func_ref);
	} else {
		compute_and_set_mangled_name(new_func_ref);
	}

	// Register the instantiation
	gTemplateRegistry.registerInstantiation(key, new_func_node);

	return new_func_node;
}

// Instantiate a lazy member function on-demand
// This performs the template parameter substitution that was deferred during lazy registration
