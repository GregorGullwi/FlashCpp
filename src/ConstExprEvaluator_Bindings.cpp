#include "Parser.h"
#include "ConstExprEvaluator.h"
#include "BuiltinListInitNarrowing.h"
#include "CallNodeHelpers.h"
#include "ExpressionSubstitutor.h"
#include "MemberFunctionLookupShared.h"
#include "OverloadResolution.h"
#include "SemanticAnalysis.h"
#include "TypeTraitEvaluator.h"
#include <algorithm>
#include <limits>

#include "ConstExprEvalHelpers.h"

namespace ConstExpr {

bool Evaluator::tryCollectConstexprOverloadResolutionArgTypes(
	const ChunkedVector<ASTNode>& arguments,
	EvaluationContext& context,
	InlineVector<TypeSpecifierNode, 6>& arg_types_out) {
	arg_types_out.clear();
	arg_types_out.reserve(arguments.size());

	auto sema_services =
		context
			.requireParserAttachedSema(kMemberFunctionMaterializationLookupOp)
			.parserSemanticServices();
	for (const ASTNode& argument : arguments) {
		std::optional<TypeSpecifierNode> arg_type =
			sema_services.getOverloadResolutionArgType(argument);
		if (!arg_type.has_value() || arg_type->category() == TypeCategory::Invalid) {
			arg_types_out.clear();
			return false;
		}
		arg_types_out.push_back(*arg_type);
	}
	return true;
}

Evaluator::ResolvedMemberFunctionCandidate Evaluator::resolveConstexprMemberCallCandidateFromCollectedSets(
	const ConstAwareMemberCandidateSet& candidate_sets,
	const ChunkedVector<ASTNode>& arguments,
	EvaluationContext& context) {
	Evaluator::ResolvedMemberFunctionCandidate result;
	if (candidate_sets.compatible.empty()) {
		return result;
	}

	InlineVector<ASTNode, 8> preferred_overloads;
	InlineVector<ASTNode, 8> compatible_overloads;
	appendUniqueMemberFunctionOverloadNodes(preferred_overloads, candidate_sets.preferred);
	appendUniqueMemberFunctionOverloadNodes(compatible_overloads, candidate_sets.compatible);

	InlineVector<TypeSpecifierNode, 6> arg_types;
	const bool has_arg_types =
		tryCollectConstexprOverloadResolutionArgTypes(arguments, context, arg_types);
	auto tryResolveCollectedSet =
		[&](std::span<const ASTNode> overloads,
			std::span<const StructMemberFunction* const> members,
			bool* ambiguous_out) -> Evaluator::ResolvedMemberFunctionCandidate {
			Evaluator::ResolvedMemberFunctionCandidate candidate_result;
			if (ambiguous_out != nullptr) {
				*ambiguous_out = false;
			}
			if (overloads.empty()) {
				return candidate_result;
			}

			if (!has_arg_types) {
				if (members.size() == 1 &&
					members.front() != nullptr &&
					members.front()->function_decl.is<FunctionDeclarationNode>()) {
					candidate_result.member = members.front();
					candidate_result.function =
						&members.front()->function_decl.as<FunctionDeclarationNode>();
					return candidate_result;
				}
				if (ambiguous_out != nullptr && members.size() > 1) {
					*ambiguous_out = true;
				}
				return candidate_result;
			}

			const OverloadResolutionResult overload_result =
				resolve_overload_with_argument_nodes(overloads, arg_types, std::span<const ASTNode>{});
			if (overload_result.is_ambiguous) {
				if (ambiguous_out != nullptr) {
					*ambiguous_out = true;
				}
				return candidate_result;
			}
			if (!overload_result.has_match || overload_result.selected_overload == nullptr ||
				!overload_result.selected_overload->is<FunctionDeclarationNode>()) {
				return candidate_result;
			}

			const auto& resolved_function =
				overload_result.selected_overload->as<FunctionDeclarationNode>();
			candidate_result.function = &resolved_function;
			candidate_result.member =
				findCollectedMemberFunctionByIdentity(members, resolved_function);
			return candidate_result;
		};

	bool preferred_ambiguous = false;
	result = tryResolveCollectedSet(
		preferred_overloads,
		candidate_sets.preferred,
		&preferred_ambiguous);
	if (result.function != nullptr || preferred_ambiguous) {
		result.ambiguous = preferred_ambiguous;
		return result;
	}

	if (!sameMemberCandidateSet(candidate_sets.preferred, candidate_sets.compatible)) {
		bool compatible_ambiguous = false;
		result = tryResolveCollectedSet(
			compatible_overloads,
			candidate_sets.compatible,
			&compatible_ambiguous);
		result.ambiguous = compatible_ambiguous;
	}

	return result;
}

Evaluator::ResolvedMemberFunctionCandidate Evaluator::resolveConstAwareConstexprMemberFunctionCallCandidate(
	const StructTypeInfo* struct_info,
	StringHandle function_name_handle,
	const ChunkedVector<ASTNode>& arguments,
	EvaluationContext& context,
	MemberFunctionLookupMode lookup_mode,
	bool require_static,
	bool receiver_is_const,
	bool stop_at_first_local_name_set) {
	const ConstAwareMemberCandidateSet candidate_sets =
		collectConstAwareVisibleMemberFunctionCandidates(
			struct_info,
			function_name_handle,
			receiver_is_const,
			stop_at_first_local_name_set,
			[&](const StructMemberFunction& member_func, const FunctionDeclarationNode& func_decl) {
				return matchesConstexprFunctionName(member_func, function_name_handle) &&
					isConstexprMemberLookupCandidate(
						func_decl,
						arguments.size(),
						context,
						lookup_mode,
						require_static);
			});
	return resolveConstexprMemberCallCandidateFromCollectedSets(
		candidate_sets,
		arguments,
		context);
}

bool Evaluator::isConstexprMemberLookupCandidate(
	const FunctionDeclarationNode& func_decl,
	size_t argument_count,
	EvaluationContext& context,
	MemberFunctionLookupMode lookup_mode,
	bool require_static) {
	if (require_static && !func_decl.is_static()) {
		return false;
	}
	const size_t parameter_count = func_decl.parameter_nodes().size();
	const size_t min_required = countMinRequiredArgs(func_decl);
	if (argument_count < min_required || argument_count > parameter_count) {
		return false;
	}

	if (lookup_mode == Evaluator::MemberFunctionLookupMode::ConstexprEvaluable) {
		bool can_evaluate = func_decl.is_constexpr() || func_decl.is_consteval() ||
							(context.storage_duration == ConstExpr::StorageDuration::Static);
		if (!can_evaluate || !func_decl.is_materialized()) {
			return false;
		}
	}

	return true;
}


const FunctionDeclarationNode* Evaluator::try_get_lowered_constexpr_member_call_target(
	const CallExprNode& call_expr,
	const StructTypeInfo* struct_info,
	size_t argument_count,
	EvaluationContext& context,
	MemberFunctionLookupMode lookup_mode,
	bool require_static) {
	const FunctionDeclarationNode* lowered_func = call_expr.callee().function_declaration_or_null();
	if (!lowered_func) {
		return nullptr;
	}
	if (struct_info) {
		for (const auto& member_func : struct_info->member_functions) {
			if (!member_func.function_decl.is<FunctionDeclarationNode>()) {
				continue;
			}
			const auto& candidate = member_func.function_decl.as<FunctionDeclarationNode>();
			if (!isConstexprMemberLookupCandidate(
					candidate,
					argument_count,
					context,
					lookup_mode,
					require_static)) {
				continue;
			}
			if (&candidate.decl_node() == &lowered_func->decl_node()) {
				return &candidate;
			}
			if (candidate.has_mangled_name() && lowered_func->has_mangled_name() &&
				candidate.mangled_name() == lowered_func->mangled_name()) {
				return &candidate;
			}
			if (sameConstexprFunctionIdentity(candidate, *lowered_func)) {
				return &candidate;
			}
		}
	}
	if (!isConstexprMemberLookupCandidate(
			*lowered_func,
			argument_count,
			context,
			lookup_mode,
			require_static)) {
		return nullptr;
	}
	const bool lowered_func_has_replay_identity =
		lowered_func->has_outer_template_bindings() ||
		lowered_func->has_non_type_template_args() ||
		lowered_func->has_mangled_name() ||
		call_expr.has_template_arguments();
	if (struct_info &&
		findMemberFunctionMetadataRecursive(struct_info, *lowered_func) == nullptr &&
		!lowered_func_has_replay_identity) {
		return nullptr;
	}
	if (struct_info && !lowered_func->parent_struct_name().empty()) {
		const StringHandle lowered_owner =
			StringTable::getOrInternStringHandle(lowered_func->parent_struct_name());
		if (normalizeClassName(lowered_owner) != normalizeClassName(struct_info->name)) {
			return nullptr;
		}
	}
	return lowered_func;
}
const ConstructorDeclarationNode* Evaluator::find_matching_constructor(
	const StructTypeInfo* struct_info,
	const ChunkedVector<ASTNode>& arguments,
	EvaluationContext& context,
	bool skip_implicit_constructors,
	const std::unordered_map<std::string_view, EvalResult>* outer_bindings) {
	if (!struct_info) {
		return nullptr;
	}

	auto ctor_candidates =
		struct_info->getConstructorsByParameterCount(arguments.size(), skip_implicit_constructors);
	if (ctor_candidates.empty()) {
		// No exact-arity match: fall back to constructors callable with this many args via default params.
		auto arity_result =
			resolve_constructor_overload_arity(*struct_info, arguments.size(), skip_implicit_constructors);
		return arity_result.selected_overload;
	}

	std::vector<TypeSpecifierNode> arg_types;
	arg_types.reserve(arguments.size());
	bool has_all_arg_types = true;
	for (const auto& argument : arguments) {
		std::optional<TypeSpecifierNode> arg_type_opt =
			Evaluator::tryQueryExpressionType(argument, context);

		if (!arg_type_opt.has_value() && argument.is<ExpressionNode>()) {
			const ExpressionNode& expr = argument.as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(expr)) {
				const IdentifierNode& identifier = std::get<IdentifierNode>(expr);
				if (context.local_bindings) {
					auto local_it = context.local_bindings->find(identifier.name());
					if (local_it != context.local_bindings->end()) {
						arg_type_opt = try_get_type_from_eval_result(local_it->second);
					}
				}
				if (!arg_type_opt.has_value() && outer_bindings) {
					auto outer_it = outer_bindings->find(identifier.name());
					if (outer_it != outer_bindings->end()) {
						arg_type_opt = try_get_type_from_eval_result(outer_it->second);
					}
				}
			}
		}

		EvalResult arg_eval_result = outer_bindings
			? evaluate_expression_with_bindings_const(argument, *outer_bindings, context)
			: evaluate(argument, context);
		if (arg_eval_result.success()) {
			if (auto constexpr_arg_type = try_get_type_from_eval_result(arg_eval_result); constexpr_arg_type.has_value()) {
				arg_type_opt = constexpr_arg_type;
			}
		}

		if (!arg_type_opt.has_value()) {
			has_all_arg_types = false;
			break;
		}

		TypeSpecifierNode arg_type = *arg_type_opt;
		adjust_argument_type_for_overload_resolution(argument, arg_type);
		arg_types.push_back(std::move(arg_type));
	}

	if (has_all_arg_types && arg_types.size() == arguments.size()) {
		auto resolution =
			resolve_constructor_overload(*struct_info, arg_types, skip_implicit_constructors);
		if (resolution.has_match) {
			return resolution.selected_overload;
		}
		if (resolution.is_ambiguous) {
			return nullptr;
		}
		if (ctor_candidates.size() == 1 && ctor_candidates[0] && ctor_candidates[0]->function_decl.is<ConstructorDeclarationNode>()) {
			const auto& only_ctor = ctor_candidates[0]->function_decl.as<ConstructorDeclarationNode>();
			if (!isImplicitCopyOrMoveConstructorCandidate(*struct_info, only_ctor)) {
				return &only_ctor;
			}
		}
		return nullptr;
	}

	const ConstructorDeclarationNode* constexpr_match = nullptr;
	const ConstructorDeclarationNode* non_constexpr_match = nullptr;
	for (const StructMemberFunction* ctor_candidate : ctor_candidates) {
		if (!ctor_candidate || !ctor_candidate->function_decl.is<ConstructorDeclarationNode>()) {
			continue;
		}
		const ConstructorDeclarationNode& ctor_decl = ctor_candidate->function_decl.as<ConstructorDeclarationNode>();
		if (ctor_decl.is_constexpr()) {
			if (constexpr_match) {
				return nullptr;
			}
			constexpr_match = &ctor_decl;
		} else {
			if (non_constexpr_match) {
				non_constexpr_match = nullptr; // ambiguous non-constexpr
				continue;
			}
			non_constexpr_match = &ctor_decl;
		}
	}
	return constexpr_match ? constexpr_match : non_constexpr_match;
}

std::optional<ASTNode> Evaluator::lookup_identifier_symbol(
	const IdentifierNode* identifier,
	std::string_view fallback_name,
	const SymbolTable& symbols) {
	if (identifier && identifier->resolved_name().isValid()) {
		auto resolved_symbol = symbols.lookup(identifier->resolved_name());
		if (resolved_symbol.has_value()) {
			return resolved_symbol;
		}
	}

	return symbols.lookup(fallback_name);
}

// Returns true if the identifier resolves to a declared array variable (not a pointer).
bool Evaluator::identifier_is_array_var(const IdentifierNode& id, EvaluationContext& context) {
	if (!context.symbols)
		return false;
	auto sym = lookup_identifier_symbol(&id, id.name(), *context.symbols);
	if (!sym.has_value() && context.global_symbols)
		sym = context.global_symbols->lookup(id.name());
	return sym.has_value() && sym->is<VariableDeclarationNode>() && sym->as<VariableDeclarationNode>().declaration().is_array();
}


std::optional<ASTNode> Evaluator::lookup_function_symbol(
	const CallExprNode& call_expr,
	std::string_view fallback_name,
	const SymbolTable& symbols) {
	if (call_expr.has_mangled_name()) {
		auto mangled_symbol = symbols.lookup(call_expr.mangled_name_handle());
		if (mangled_symbol.has_value()) {
			return mangled_symbol;
		}
	}

	if (call_expr.has_qualified_name()) {
		QualifiedIdentifier qi = QualifiedIdentifier::fromQualifiedName(
			call_expr.qualified_name_handle(),
			symbols.get_current_namespace_handle());
		auto qualified_symbol = symbols.lookup_qualified(qi);
		if (qualified_symbol.has_value()) {
			return qualified_symbol;
		}
	}

	return symbols.lookup(fallback_name);
}
EvalResult Evaluator::write_value_to_bound_lvalue(
	const ASTNode& target_expr,
	const EvalResult& value,
	std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context) {
	std::optional<EvalResult> resolve_error;
	std::optional<BoundWriteTarget> target = resolveBoundWriteTarget(
		target_expr, bindings, context, evaluate_expression_with_bindings_const, resolve_error);
	if (!target.has_value() || target->slot == nullptr) {
		if (resolve_error.has_value()) {
			return *resolve_error;
		}
		return EvalResult::error("Unsupported by-reference init-capture alias target");
	}

	*target->slot = value;
	if (!target->root_name.empty()) {
		if (EvalResult* root_binding = findMutableBindingValue(target->root_name, bindings, context)) {
			refreshPointerSnapshotsForBinding(target->root_name, *root_binding, bindings, context);
		}
	}

	return value;
}

EvalResult Evaluator::evaluate_function_call_with_outer_bindings(
	const CallExprNode& call_expr,
	const std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context,
	std::unordered_map<std::string_view, EvalResult>* mutable_bindings) {
	const DeclarationNode& func_decl_node = call_expr.callee().declaration();
	std::string_view func_name = func_decl_node.identifier_token().value();

	if (!context.symbols) {
		return EvalResult::error("Cannot evaluate function call: no symbol table provided");
	}

	const EvalResult* bound_callable = findBindingValue(func_name, bindings, context);
	if (bound_callable && (bound_callable->callable_var_decl || bound_callable->callable_lambda)) {
		EvalResult* mutable_bound_callable = nullptr;
		if (mutable_bindings) {
			mutable_bound_callable = findMutableBindingValue(func_name, *mutable_bindings, context);
		}
		if (bound_callable->callable_var_decl) {
			return evaluate_callable_object(
				*bound_callable->callable_var_decl,
				call_expr.arguments(),
				context,
				&bindings,
				mutable_bindings,
				mutable_bound_callable,
				call_expr.callee().function_declaration_or_null());
		}
		return evaluate_lambda_call(*bound_callable->callable_lambda, call_expr.arguments(), context, &bindings, mutable_bindings,
									&bound_callable->callable_bindings,
									mutable_bound_callable ? &mutable_bound_callable->callable_bindings : nullptr);
	}

	if (!call_expr.has_qualified_name() && context.struct_info) {
		StringHandle func_name_handle = StringTable::getOrInternStringHandle(func_name);
		auto current_struct_match = resolveConstAwareConstexprMemberFunctionCallCandidate(
			context.struct_info,
			func_name_handle,
			call_expr.arguments(),
			context,
			MemberFunctionLookupMode::ConstexprEvaluable,
			false,
			context.current_member_function_is_const,
			true);
		if (current_struct_match.ambiguous) {
			return EvalResult::error("Ambiguous member function overload in constant expression");
		}
		if (current_struct_match.function) {
			if (!current_struct_match.function->is_static()) {
				Token this_token(
					Token::Type::Identifier,
					"this"sv,
					call_expr.called_from().line(),
					call_expr.called_from().column(),
					call_expr.called_from().file_index());
				ExpressionNode this_expr = IdentifierNode(this_token);
				CallExprNode member_call = makeResolvedMemberCallExpr(
					ASTNode(&this_expr),
					*current_struct_match.function,
					copyCallArguments(call_expr.arguments()),
					call_expr.called_from());
				ExpressionNode member_expr = member_call;
				if (auto bound_member_result = try_evaluate_bound_member_function_call(member_expr, bindings, context, mutable_bindings)) {
					return *bound_member_result;
				}
				return evaluate_member_function_call(member_call, context);
			}
			return evaluate_function_call_with_bindings(
				*current_struct_match.function,
				call_expr.arguments(),
				bindings,
				context,
				mutable_bindings);
		}
	}

	if (call_expr.has_dependent_unqualified_lookup_record()) {
		const DependentUnqualifiedCallLookupRecord& dependent_record =
			*call_expr.dependent_unqualified_lookup_record();
		if (dependent_record.point_of_instantiation_function != nullptr) {
			return evaluate_function_call_with_bindings(
				*dependent_record.point_of_instantiation_function,
				call_expr.arguments(),
				bindings,
				context,
				mutable_bindings);
		}
		// POI resolution requires parser-owned context. Missing parser/sema is a
		// contract violation and must hard-fail.
		SemanticAnalysis& sema_ref =
			context.requireParserOwnedSema("dependent unqualified member call POI resolution");
		// The sema pass may have already resolved this call during annotation.
		// Consume that pre-resolved result directly instead of re-running POI lookup.
		auto sema_services = sema_ref.parserSemanticServices();
		ResolvedFunctionQueryResult sema_query =
			sema_services.getResolvedDirectCallQuery(&call_expr);
		switch (sema_query.state) {
		case ResolvedFunctionQueryResult::State::Available:
			if (sema_query.function == nullptr) {
				throw InternalError(
					"Dependent unqualified member call query reported Available without a function");
			}
			return evaluate_function_call_with_bindings(
				*sema_query.function,
				call_expr.arguments(),
				bindings,
				context,
				mutable_bindings);
		case ResolvedFunctionQueryResult::State::NotYetAnalyzed:
			if (sema_services.hasPostParseNormalizationStarted()) {
				throw InternalError("Normalized dependent unqualified member direct-call query remained NotYetAnalyzed");
			}
			break;
		case ResolvedFunctionQueryResult::State::AnalyzedAbsent:
			break;
		}
		// Sema query was absent or not yet analyzed — fall back to parser POI lookup.
		Parser& parser = *context.parser;
		std::vector<TypeSpecifierNode> arg_types;
		if (!parser.tryCollectFunctionCallArgTypes(call_expr.arguments(), arg_types)) {
			return EvalResult::error(
				"Dependent unqualified call argument types are not available at point of instantiation",
				EvalErrorType::TemplateDependentExpression);
		}
		std::optional<ASTNode> resolved_target =
			parser.resolveDependentUnqualifiedCallAtPointOfInstantiation(
				dependent_record,
				call_expr.arguments(),
				arg_types);
		if (!resolved_target.has_value()) {
			return EvalResult::error(
				"Dependent unqualified call could not be resolved at point of instantiation",
				EvalErrorType::TemplateDependentExpression);
		}
		if (const FunctionDeclarationNode* resolved_function =
				get_function_decl_node(*resolved_target);
			resolved_function != nullptr) {
			return evaluate_function_call_with_bindings(
				*resolved_function,
				call_expr.arguments(),
				bindings,
				context,
				mutable_bindings);
		}
		return EvalResult::error(
			"Dependent unqualified call resolved to a non-function target",
			EvalErrorType::TemplateDependentExpression);
	}

	if (const FunctionDeclarationNode* resolved_function = call_expr.callee().function_declaration_or_null()) {
		if (resolved_function->is_static() || !context.struct_info) {
			return evaluate_function_call_with_bindings(
				*resolved_function,
				call_expr.arguments(),
				bindings,
				context,
				mutable_bindings);
		}
	}

	auto symbol_opt = lookup_function_symbol(call_expr, func_name, *context.symbols);
	if (!symbol_opt.has_value()) {
		if (call_expr.has_template_arguments() && context.parser) {
			auto var_result = tryEvaluateAsVariableTemplate(func_name, call_expr, context);
			if (var_result.success()) {
				return var_result;
			}
		}
		if (hasDependentTemplateArguments(call_expr, context)) {
			return EvalResult::error(
				"Dependent function/variable template call in constant expression: " +
					std::string(func_name),
				EvalErrorType::TemplateDependentExpression);
		}
		return EvalResult::error("Undefined function in constant expression: " + std::string(func_name));
	}

	const ASTNode& symbol_node = symbol_opt.value();
	if (!symbol_node.is<FunctionDeclarationNode>()) {
		if (symbol_node.is<VariableDeclarationNode>()) {
			const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
			return evaluate_callable_object(
				var_decl,
				call_expr.arguments(),
				context,
				&bindings,
				mutable_bindings,
				nullptr,
				call_expr.callee().function_declaration_or_null());
		}

		if (symbol_node.is<TemplateVariableDeclarationNode>()) {
			auto var_result = tryEvaluateAsVariableTemplate(func_name, call_expr, context);
			if (var_result.success()) {
				return var_result;
			}
		}
		return EvalResult::error("Identifier is not a function: " + std::string(func_name));
	}

	const FunctionDeclarationNode& func_decl = symbol_node.as<FunctionDeclarationNode>();
	if (!func_decl.is_constexpr() && !func_decl.is_consteval() &&
		context.storage_duration != ConstExpr::StorageDuration::Static) {
		return EvalResult::error("Function in constant expression must be constexpr or consteval: " + std::string(func_name),
								 EvalErrorType::NotConstantExpression);
	}

	if (!func_decl.is_static() && context.struct_info) {
		Token this_token(
			Token::Type::Identifier,
			"this"sv,
			call_expr.called_from().line(),
			call_expr.called_from().column(),
			call_expr.called_from().file_index());
		ExpressionNode this_expr = IdentifierNode(this_token);
		CallExprNode member_call = makeResolvedMemberCallExpr(
			ASTNode(&this_expr),
			func_decl,
			copyCallArguments(call_expr.arguments()),
			call_expr.called_from());
		ExpressionNode member_expr = member_call;
		if (auto bound_member_result = try_evaluate_bound_member_function_call(member_expr, bindings, context, mutable_bindings)) {
			return *bound_member_result;
		}
		return evaluate_member_function_call(member_call, context);
	}

	return evaluate_function_call_with_bindings(func_decl, call_expr.arguments(), bindings, context, mutable_bindings);
}

std::optional<EvalResult> Evaluator::try_evaluate_bound_member_operator_call(
	const ExpressionNode& expr,
	const std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context,
	std::unordered_map<std::string_view, EvalResult>* mutable_bindings) {
	const std::optional<CallInfo> call_info = CallInfo::tryFrom(expr);
	if (!call_info || !call_info->has_receiver || !call_info->function_declaration) {
		return std::nullopt;
	}

	std::string_view func_name = normalizeConstexprLookupName(
		call_info->function_declaration->decl_node().identifier_token().value());
	if (overloadableOperatorFromFunctionName(func_name) != OverloadableOperator::Call) {
		return std::nullopt;
	}

	auto extract_callable_identifier = [&]() -> const IdentifierNode* {
		return tryGetIdentifier(call_info->receiver);
	};

	auto extract_lambda_from_object_expr = [&]() -> const LambdaExpressionNode* {
		const ASTNode& object_expr = call_info->receiver;
		if (object_expr.is<LambdaExpressionNode>()) {
			return &object_expr.as<LambdaExpressionNode>();
		}
		if (object_expr.is<ExpressionNode>()) {
			const ExpressionNode& expr_node = object_expr.as<ExpressionNode>();
			if (const auto* lambda_expression = std::get_if<LambdaExpressionNode>(&expr_node)) {
				return lambda_expression;
			}
		}
		return nullptr;
	};

	if (const LambdaExpressionNode* lambda = extract_lambda_from_object_expr()) {
		return evaluate_lambda_call(*lambda, *call_info->arguments, context, &bindings, mutable_bindings);
	}

	if (const IdentifierNode* callable_id = extract_callable_identifier()) {
		const EvalResult* callable_value = findBindingValue(callable_id->name(), bindings, context);
		if (callable_value && (callable_value->callable_var_decl || callable_value->callable_lambda)) {
			EvalResult* mutable_bound_callable = nullptr;
			if (mutable_bindings) {
				mutable_bound_callable = findMutableBindingValue(callable_id->name(), *mutable_bindings, context);
			}
			if (callable_value->callable_var_decl) {
				return evaluate_callable_object(
					*callable_value->callable_var_decl,
					*call_info->arguments,
					context,
					&bindings,
					mutable_bindings,
					mutable_bound_callable,
					call_info->function_declaration);
			}
			return evaluate_lambda_call(*callable_value->callable_lambda, *call_info->arguments, context, &bindings, mutable_bindings,
										&callable_value->callable_bindings,
										mutable_bound_callable ? &mutable_bound_callable->callable_bindings : nullptr);
		}
	}

	return std::nullopt;
}

Evaluator::ResolvedBoundEvalResult Evaluator::resolve_bound_eval_result(
	const ASTNode& bound_expr,
	const std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context,
	bool treat_this_as_unbound) {
	ResolvedBoundEvalResult resolved;

	if (bound_expr.is<IdentifierNode>()) {
		std::string_view bound_name = bound_expr.as<IdentifierNode>().name();
		if (treat_this_as_unbound && bound_name == "this") {
			return resolved;
		}

		const EvalResult* bound_value = findBindingValue(bound_name, bindings, context);
		if (!bound_value) {
			return resolved;
		}

		resolved.value = bound_value;
		return resolved;
	}

	if (bound_expr.is<ExpressionNode>()) {
		const ExpressionNode& bound_expr_node = bound_expr.as<ExpressionNode>();
		if (treat_this_as_unbound &&
			std::holds_alternative<IdentifierNode>(bound_expr_node) &&
			std::get<IdentifierNode>(bound_expr_node).name() == "this") {
			return resolved;
		}

		resolved.owned_value = evaluate_expression_with_bindings_const(bound_expr, bindings, context);
		if (!resolved.owned_value->success()) {
			resolved.error = resolved.owned_value;
			return resolved;
		}

		resolved.value = &resolved.owned_value.value();
		return resolved;
	}

	resolved.owned_value = evaluate_with_optional_bindings(bound_expr, context, &bindings, nullptr);
	if (!resolved.owned_value->success()) {
		resolved.error = resolved.owned_value;
		return resolved;
	}

	resolved.value = &resolved.owned_value.value();
	return resolved;
}

std::optional<EvalResult> Evaluator::try_evaluate_bound_member_access(
	const ExpressionNode& expr,
	const std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context) {
	if (!std::holds_alternative<MemberAccessNode>(expr)) {
		return std::nullopt;
	}

	const auto& member_access = std::get<MemberAccessNode>(expr);
	const ASTNode& object_expr = member_access.object();
	if (const IdentifierNode* object_id = tryGetIdentifier(object_expr);
		object_id && object_id->name() == "this") {
		// Prefer the current direct member bindings over the synthetic "this"
		// object snapshot so reads after prior member mutations observe the latest state.
		auto direct_member_it = bindings.find(member_access.member_name());
		if (direct_member_it != bindings.end()) {
			return validateConstexprRead(direct_member_it->second);
		}
	}
	ResolvedBoundEvalResult resolved_object = resolve_bound_eval_result(object_expr, bindings, context, true);
	if (resolved_object.error.has_value()) {
		return resolved_object.error.value();
	}

	std::optional<EvalResult> owned_object_result;
	const EvalResult* object_result = resolved_object.value;
	if (!object_result) {
		auto evaluated_object = evaluate_expression_with_bindings_const(object_expr, bindings, context);
		if (!evaluated_object.success()) {
			return evaluated_object;
		}
		owned_object_result = std::move(evaluated_object);
		object_result = &owned_object_result.value();
	}

	// For arrow member access (p->x) on a heap-allocated struct, dereference the
	// pointer first to get the struct's member bindings from the constexpr heap.
	if (member_access.is_arrow() && object_result->pointer_to_var.isValid() &&
		!context.constexpr_heap.empty()) {
		StringHandle heap_key = object_result->pointer_to_var;
		auto heap_it = context.constexpr_heap.find(heap_key);
		if (heap_it != context.constexpr_heap.end()) {
			if (heap_it->second.freed) {
				return EvalResult::error("Arrow member access on freed heap pointer: use after free in constant expression");
			}
			const EvalResult& heap_obj = heap_it->second.value;
			int64_t offset = object_result->pointer_offset;
			const EvalResult* target_obj = &heap_obj;
			if (heap_obj.is_array) {
				if (offset < 0 || static_cast<size_t>(offset) >= heap_obj.array_elements.size()) {
					return EvalResult::error("Arrow member access: pointer offset out of bounds for heap struct array");
				}
				target_obj = &heap_obj.array_elements[static_cast<size_t>(offset)];
			} else if (offset != 0) {
				return EvalResult::error("Arrow member access: non-zero pointer offset on non-array heap object");
			}
			auto member_it = target_obj->object_member_bindings.find(member_access.member_name());
			if (member_it != target_obj->object_member_bindings.end()) {
				return validateConstexprRead(member_it->second);
			}
			// The heap entry exists but the requested member name is absent.  Return an
			// explicit diagnostic here rather than falling through to std::nullopt which
			// would let the non-heap path run and produce the confusing
			// "could not resolve pointed-to object '@new_N'" error message.
			return EvalResult::error("Member '" + std::string(member_access.member_name()) +
									 "' not found on heap-allocated object in constant expression");
		}
		return std::nullopt;
	}
	if (member_access.is_arrow() && object_result->pointer_to_var.isValid()) {
		auto deref_result = deref_pointer_with_bindings(*object_result, bindings, context);
		if (!deref_result.success()) {
			if (!isRecoverablePointerDerefFailure(deref_result)) {
				return deref_result;
			}
			return std::nullopt;
		}
		auto member_it = deref_result.object_member_bindings.find(member_access.member_name());
		if (member_it != deref_result.object_member_bindings.end()) {
			return validateConstexprRead(member_it->second);
		}
		if (object_result->pointer_offset == 0) {
			return std::nullopt;
		}
		return EvalResult::error(
			"Member '" + std::string(member_access.member_name()) +
			"' not found on pointed-to object in constant expression");
	}

	if (!object_result->object_type_index.is_valid()) {
		return std::nullopt;
	}

	auto member_it = object_result->object_member_bindings.find(member_access.member_name());
	if (member_it == object_result->object_member_bindings.end()) {
		return std::nullopt;
	}

	return validateConstexprRead(member_it->second);
}

std::optional<EvalResult> Evaluator::try_evaluate_bound_array_subscript(
	const ExpressionNode& expr,
	const std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context) {
	if (!std::holds_alternative<ArraySubscriptNode>(expr)) {
		return std::nullopt;
	}

	const auto& subscript = std::get<ArraySubscriptNode>(expr);
	auto index_result = evaluate_expression_with_bindings_const(subscript.index_expr(), bindings, context);
	if (!index_result.success()) {
		return index_result;
	}

	long long index = index_result.as_int();
	if (index < 0) {
		return EvalResult::error("Negative array index in constant expression");
	}

	const ASTNode& array_expr = subscript.array_expr();
	ResolvedBoundEvalResult resolved_array = resolve_bound_eval_result(array_expr, bindings, context);
	if (resolved_array.error.has_value()) {
		return resolved_array.error.value();
	}

	const EvalResult* array_result = resolved_array.value;
	if (!array_result) {
		return std::nullopt;
	}
	std::optional<EvalResult> recovered_object_value;
	if (!array_result->is_array &&
		!array_result->object_type_index.is_valid()) {
		const IdentifierNode* object_id = tryGetIdentifier(array_expr);
		if (object_id && context.symbols) {
			std::optional<ASTNode> symbol_opt = lookup_identifier_symbol(
				object_id,
				object_id->name(),
				*context.symbols);
			if (!symbol_opt.has_value() && context.global_symbols) {
				symbol_opt = lookup_identifier_symbol(object_id, object_id->name(), *context.global_symbols);
			}
			if (symbol_opt.has_value() && symbol_opt->is<VariableDeclarationNode>()) {
				const VariableDeclarationNode& var_decl = symbol_opt->as<VariableDeclarationNode>();
				if (var_decl.is_constexpr() && var_decl.initializer().has_value()) {
					const TypeSpecifierNode& decl_type_spec = var_decl.declaration().type_specifier_node();
					TypeIndex declared_type_index = decl_type_spec.type_index();
					const TypeInfo* declared_type_info = tryGetTypeInfo(declared_type_index);
					if (!declared_type_info) {
						StringHandle type_name_handle = decl_type_spec.token().handle();
						auto type_it = getTypesByNameMap().find(type_name_handle);
						if (type_it != getTypesByNameMap().end()) {
							declared_type_info = type_it->second;
							if (!declared_type_index.is_valid()) {
								declared_type_index = declared_type_info->type_index_;
							}
						}
					}
					const StructTypeInfo* declared_struct_info =
						declared_type_info ? declared_type_info->getStructInfo() : nullptr;
					if (declared_struct_info) {
						const ASTNode& init_node = var_decl.initializer().value();
						if (const ConstructorCallNode* ctor_call = extract_constructor_call(init_node)) {
							recovered_object_value = materialize_constructor_object_value(*ctor_call, context, &bindings);
						} else if (init_node.is<InitializerListNode>()) {
							recovered_object_value = materialize_aggregate_object_value(
								declared_struct_info,
								declared_type_index,
								init_node.as<InitializerListNode>(),
								context,
								&bindings);
						} else if (!declared_struct_info->hasUserDeclaredConstructor()) {
							InitializerListNode singleton_init;
							singleton_init.add_initializer(init_node);
							recovered_object_value = materialize_aggregate_object_value(
								declared_struct_info,
								declared_type_index,
								singleton_init,
								context,
								&bindings);
						}
						if (recovered_object_value.has_value() && recovered_object_value->success()) {
							array_result = &*recovered_object_value;
						}
					}
				}
			}
		}
	}
	// Handle pointer subscript: ptr[i] → *(ptr + i)
	if (array_result->pointer_to_var.isValid()) {
		EvalResult offset_ptr = *array_result;
		offset_ptr.pointer_offset += index;
		return deref_pointer_with_bindings(offset_ptr, bindings, context);
	}
	// Handle struct with operator[]: dispatch to the member function when the
	// subscripted expression resolves to a struct object with operator[] defined.
	if (!array_result->is_array && array_result->object_type_index.is_valid()) {
		const TypeInfo* type_info = tryGetTypeInfo(array_result->object_type_index);
		const StructTypeInfo* struct_info = type_info ? type_info->getStructInfo() : nullptr;
		if (struct_info) {
			StringHandle op_bracket = StringTable::getOrInternStringHandle("operator[]");
			bool object_is_const = array_result->exact_type.has_value() &&
				array_result->exact_type->cv_qualifier() == CVQualifier::Const;
			if (!object_is_const) {
				const IdentifierNode* object_id = tryGetIdentifier(array_expr);
				if (object_id && context.symbols) {
					std::optional<ASTNode> symbol_opt = lookup_identifier_symbol(
						object_id,
						object_id->name(),
						*context.symbols);
					if (!symbol_opt.has_value() && context.global_symbols) {
						symbol_opt = lookup_identifier_symbol(object_id, object_id->name(), *context.global_symbols);
					}
					if (symbol_opt.has_value() && symbol_opt->is<VariableDeclarationNode>()) {
						const VariableDeclarationNode& var_decl = symbol_opt->as<VariableDeclarationNode>();
						const TypeSpecifierNode& decl_type = var_decl.declaration().type_specifier_node();
						object_is_const = var_decl.is_constexpr() ||
							decl_type.cv_qualifier() == CVQualifier::Const;
					}
				}
			}
			if (!object_is_const) {
				auto expr_type = Evaluator::tryQueryExpressionType(array_expr, context);
				object_is_const = expr_type.has_value() &&
					expr_type->cv_qualifier() == CVQualifier::Const;
			}
			ResolvedMemberFunctionCandidate candidate =
				findConstexprOperatorOverload(struct_info, op_bracket, 1, context);
			if (candidate.ambiguous) {
				return EvalResult::error("Ambiguous operator[] overload in constant expression");
			}
			if (candidate.function) {
				return invokeConstexprMemberFunction(
					*candidate.function,
					array_result->object_member_bindings,
					array_result->object_type_index,
					type_info,
					struct_info,
					{index_result},
					context,
					"operator[] body is not a block",
					"operator[] did not return a value");
			}
		}
	}
	if (!array_result->is_array) {
		return EvalResult::error("Subscript on non-array variable in constant expression");
	}
	if (!array_result->array_elements.empty()) {
		if (static_cast<size_t>(index) >= array_result->array_elements.size()) {
			return EvalResult::error("Array index out of bounds in constant expression");
		}
		return array_result->array_elements[static_cast<size_t>(index)];
	}
	if (static_cast<size_t>(index) >= array_result->array_values.size()) {
		return EvalResult::error("Array index out of bounds in constant expression");
	}

	return EvalResult::from_int(array_result->array_values[static_cast<size_t>(index)]);
}

std::optional<EvalResult> Evaluator::try_evaluate_bound_member_function_call(
	const ExpressionNode& expr,
	const std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context,
	std::unordered_map<std::string_view, EvalResult>* mutable_bindings) {
	const std::optional<CallInfo> call_info = CallInfo::tryFrom(expr);
	if (!call_info || !call_info->has_receiver || !call_info->function_declaration) {
		return std::nullopt;
	}

	std::string_view func_name = normalizeConstexprLookupName(
		call_info->function_declaration->decl_node().identifier_token().value());
	// operator() on a bound local struct object falls through to this function
	// when try_evaluate_bound_member_operator_call could not resolve it via a
	// stored lambda or callable_var_decl.  The caller returns early on the first
	// success, so there is no double-evaluation risk.

	const IdentifierNode* object_identifier = tryGetIdentifier(call_info->receiver);
	const bool receiver_is_this = object_identifier && object_identifier->name() == "this";

	const StructTypeInfo* bound_struct_info = nullptr;
	TypeIndex bound_type_index{};
	const TypeInfo* bound_type_info = nullptr;
	std::unordered_map<std::string_view, EvalResult> member_bindings;
	bool write_back_to_object_binding = false;
	bool write_back_through_pointer = false;
	bool write_back_via_receiver_lvalue = false;
	bool bound_receiver_is_const = receiver_is_this && context.current_member_function_is_const;
	std::string_view object_name;
	EvalResult pointed_object_result;

	if (receiver_is_this) {
		if (!context.struct_info) {
			return std::nullopt;
		}
		bound_struct_info = context.struct_info;
		bound_type_index = context.struct_type_index;
		for (const auto& member : context.struct_info->members) {
			std::string_view member_name = StringTable::getStringView(member.getName());
			auto binding_it = bindings.find(member_name);
			if (binding_it != bindings.end()) {
				member_bindings[member_name] = binding_it->second;
			}
		}
	} else {
		const EvalResult* object_value = nullptr;
		std::optional<EvalResult> resolved_complex_object;
		// Tracks the name of the binding that the complex receiver resolved to (e.g. the
		// variable selected by a ternary condition).  When set it lets us write back
		// mutations from a non-`this`, non-simple-identifier receiver (such as
		// `(cond ? x : y).mutate()`) to the correct named local or member binding.
		std::string_view resolved_binding_name;
		// Recursively evaluate a non-identifier receiver expression to find the struct
		// object being called on.  Handles ternary conditions by evaluating the condition
		// and recursing into the chosen branch.  Also records resolved_binding_name so
		// that mutating calls can write the modified member state back to the right local.
		auto resolve_bound_receiver_object = [&](const ASTNode& receiver_expr, auto&& self) -> EvalResult {
			if (const IdentifierNode* receiver_id = tryGetIdentifier(receiver_expr)) {
				if (const EvalResult* bound_value = findBindingValue(receiver_id->name(), bindings, context)) {
					resolved_binding_name = receiver_id->name();
					return *bound_value;
				}
				return EvalResult::error(
					"Template parameter or undefined variable in constant expression: " +
					std::string(receiver_id->name()));
			}
			if (receiver_expr.is<ExpressionNode>()) {
				const ExpressionNode& receiver_node = receiver_expr.as<ExpressionNode>();
				if (const auto* ternary = std::get_if<TernaryOperatorNode>(&receiver_node)) {
					EvalResult cond_result = evaluate_expression_with_bindings_const(ternary->condition(), bindings, context);
					if (!cond_result.success()) {
						return cond_result;
					}
					return cond_result.pointer_to_var.isValid() ? self(ternary->true_expr(), self)
															 : self(cond_result.as_bool() ? ternary->true_expr() : ternary->false_expr(), self);
				}
			}
			return evaluate_expression_with_bindings_const(receiver_expr, bindings, context);
		};
		if (object_identifier) {
			object_name = object_identifier->name();
			object_value = findBindingValue(object_name, bindings, context);
		} else {
			resolved_complex_object = resolve_bound_receiver_object(call_info->receiver, resolve_bound_receiver_object);
			if (!resolved_complex_object->success()) {
				return *resolved_complex_object;
			}
			object_value = &resolved_complex_object.value();
			// If the receiver expression resolved to a named binding, record the name
			// so that the write-back path below can update the correct local/member.
			if (!resolved_binding_name.empty()) {
				object_name = resolved_binding_name;
			}
		}
		if (!object_value) {
			return std::nullopt;
		}

		if (object_value->pointer_to_var.isValid()) {
			const bool is_reference_alias_receiver = isReferenceAliasBinding(*object_value);
			pointed_object_result = deref_pointer_with_bindings(*object_value, bindings, context);
			if (!pointed_object_result.success()) {
				return pointed_object_result;
			}
			if (!pointed_object_result.object_type_index.is_valid()) {
				return EvalResult::error("Bound constexpr pointer does not point to a struct object");
			}
			bound_type_index = pointed_object_result.object_type_index;
			member_bindings = pointed_object_result.object_member_bindings;
			bound_receiver_is_const =
				(pointed_object_result.exact_type.has_value() &&
				 pointed_object_result.exact_type->is_const()) ||
				(object_value->exact_type.has_value() &&
				 object_value->exact_type->is_const());
			if (is_reference_alias_receiver) {
				// Local reference variables are alias bindings (pointer + reference-qualified type).
				// Write back through the receiver lvalue to update the alias target, avoiding
				// invalid `*receiver` expressions.
				write_back_to_object_binding = object_identifier != nullptr || !resolved_binding_name.empty();
				write_back_via_receiver_lvalue = write_back_to_object_binding;
			} else {
				// Raw pointer receiver (`ptr->fn()`): write back through `*ptr`.
				write_back_through_pointer = object_identifier != nullptr || !resolved_binding_name.empty();
			}
		} else {
			if (!object_value->object_type_index.is_valid()) {
				return std::nullopt;
			}
			bound_type_index = object_value->object_type_index;
			member_bindings = object_value->object_member_bindings;
			bound_receiver_is_const =
				object_value->exact_type.has_value() &&
				object_value->exact_type->is_const();
			// Enable write-back when the receiver is a named identifier OR when the
			// complex receiver resolved to a named binding (e.g. ternary -> identifier).
			write_back_to_object_binding = object_identifier != nullptr || !resolved_binding_name.empty();
		}

		bound_type_info = tryGetTypeInfo(bound_type_index);
		if (!bound_type_info) {
			return EvalResult::error("Invalid bound object type for constexpr member function call");
		}
		bound_struct_info = bound_type_info->getStructInfo();
		if (!bound_struct_info) {
			return EvalResult::error("Bound constexpr object is not a struct");
		}
	}

	StringHandle func_name_handle = StringTable::getOrInternStringHandle(func_name);
	auto lookup_sema_services =
		context
			.requireParserAttachedSema(kMemberFunctionMaterializationLookupOp)
			.parserSemanticServices();
	lookup_sema_services.ensureMemberFunctionMaterialized(
		bound_struct_info->name,
		func_name_handle,
		std::nullopt);
	if (call_info->function_declaration) {
		lookup_sema_services.ensureMemberFunctionMaterialized(
			bound_struct_info->name,
			*call_info->function_declaration);
	}
	const CallExprNode* call_expr = std::get_if<CallExprNode>(&expr);
	ResolvedFunctionQueryResult sema_resolved_direct_query =
		lookup_sema_services.getResolvedDirectCallQuery(call_expr);
	const FunctionDeclarationNode* actual_func =
		sema_resolved_direct_query.state == ResolvedFunctionQueryResult::State::Available
			? sema_resolved_direct_query.function
			: [&]() -> const FunctionDeclarationNode* {
				if (call_expr) {
					return try_get_lowered_constexpr_member_call_target(
						*call_expr,
						bound_struct_info,
						call_info->arguments->size(),
						context,
						MemberFunctionLookupMode::LookupOnly,
						false);
				}
				return nullptr;
			}();
	const StructMemberFunction* actual_member =
		actual_func ? findMemberFunctionMetadataRecursive(bound_struct_info, *actual_func) : nullptr;
	if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
		FLASH_LOG(ConstExpr, Debug,
			"try_evaluate_bound_member_function_call: func='", func_name,
			"' struct='", StringTable::getStringView(bound_struct_info->name),
			"' type_index=", bound_type_index,
			" sema_state=", static_cast<int>(sema_resolved_direct_query.state),
			" actual_func=", actual_func != nullptr,
			" actual_member=", actual_member != nullptr);
		if (actual_func) {
			FLASH_LOG(ConstExpr, Debug,
				"  selected owner='", actual_func->parent_struct_name(),
				"' name='", actual_func->decl_node().identifier_token().value(),
				"' const=", actual_func->is_const_member_function());
		}
		for (const auto& [member_name, member_value] : member_bindings) {
			FLASH_LOG(ConstExpr, Debug,
				"  member binding '", member_name,
				"' int=", member_value.success() ? member_value.as_int() : 0,
				" object_type=", member_value.object_type_index);
		}
	}
	if (actual_func &&
		receiver_is_this &&
		context.current_member_function_is_const &&
		!actual_func->is_static() &&
		!actual_func->is_const_member_function()) {
		actual_func = nullptr;
		actual_member = nullptr;
	}
	if (!actual_func) {
		auto member_function_match = receiver_is_this
										 ? resolveConstAwareConstexprMemberFunctionCallCandidate(
											   context.struct_info,
											   func_name_handle,
											   *call_info->arguments,
											   context,
											   MemberFunctionLookupMode::LookupOnly,
											   false,
											   context.current_member_function_is_const,
											   true)
										 : resolveConstAwareConstexprMemberFunctionCallCandidate(
											   bound_struct_info,
											   func_name_handle,
											   *call_info->arguments,
											   context,
											   MemberFunctionLookupMode::LookupOnly,
											   false,
											   bound_receiver_is_const,
											   true);
		if (member_function_match.ambiguous) {
			return EvalResult::error("Ambiguous member function overload in constant expression");
		}

		actual_func = member_function_match.function;
		actual_member = member_function_match.member;
	}
	if (actual_member && actual_member->function_decl.is<FunctionDeclarationNode>() && actual_func) {
		const auto& member_function_decl = actual_member->function_decl.as<FunctionDeclarationNode>();
		if (!sameConstexprFunctionIdentity(member_function_decl, *actual_func)) {
			actual_member = nullptr;
		}
	}
	// Virtual dispatch: when the resolved function is virtual, find the final
	// overrider in the dynamic type (bound_struct_info) rather than staying pinned
	// to the sema-resolved static target.  This applies to all receiver kinds,
	// including calls through `this`, since virtual dispatch must occur for all
	// non-qualified virtual calls per C++20 [class.virtual].
	if (actual_func && actual_member && actual_member->is_virtual) {
		if (const StructMemberFunction* override_member = findFinalOverrider(
				bound_struct_info,
				*actual_member,
				actual_func->parameter_nodes().size())) {
			actual_func = &override_member->function_decl.as<FunctionDeclarationNode>();
			actual_member = override_member;
		}
	}
	if (actual_func && !actual_func->get_definition().has_value()) {
		if (const FunctionDeclarationNode* symbol_table_func =
				findMatchingSymbolTableMemberDefinition(*actual_func, context);
			symbol_table_func &&
			sameConstexprFunctionIdentity(*symbol_table_func, *actual_func)) {
			actual_func = symbol_table_func;
		}
	}
	if (actual_func) {
		if (!actual_func->get_definition().has_value()) {
			const FunctionDeclarationNode* requested_func = actual_func;
			StringHandle owner_name = bound_struct_info->name;
			if (!actual_func->parent_struct_name().empty()) {
				owner_name = StringTable::getOrInternStringHandle(actual_func->parent_struct_name());
			}
			auto replay_sema_services =
				context
					.requireParserAttachedSema(kMemberFunctionMaterializationReplayOp)
					.parserSemanticServices();
			if (std::optional<ASTNode> refreshed =
					replay_sema_services.ensureMemberFunctionMaterialized(owner_name, *actual_func);
				refreshed.has_value() && refreshed->is<FunctionDeclarationNode>()) {
				const auto& refreshed_func = refreshed->as<FunctionDeclarationNode>();
				if (sameConstexprFunctionIdentity(refreshed_func, *requested_func)) {
					actual_func = &refreshed_func;
				}
			}
		}
	}
	if (!actual_func) {
		return EvalResult::error("Member function not found: " + std::string(func_name));
	}
	if (!actual_func->is_constexpr() && !actual_func->is_consteval() &&
		context.storage_duration != ConstExpr::StorageDuration::Static) {
		return EvalResult::error("Member function must be constexpr or consteval: " + std::string(func_name),
								 EvalErrorType::NotConstantExpression);
	}

	const auto& definition = actual_func->get_definition();
	if (!definition.has_value()) {
		return EvalResult::error("Constexpr member function has no body: " + std::string(func_name));
	}
	if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug) && definition->is<BlockNode>()) {
		const auto& stmts = definition->as<BlockNode>().get_statements();
		if (!stmts.empty() && stmts[0].is<ReturnStatementNode>()) {
			const auto& ret = stmts[0].as<ReturnStatementNode>();
			if (ret.expression().has_value() && ret.expression()->is<ExpressionNode>()) {
				FLASH_LOG(ConstExpr, Debug,
					"  first return expr index=", ret.expression()->as<ExpressionNode>().index());
			}
		}
	}

	auto bind_result = bind_evaluated_arguments(
		actual_func->parameter_nodes(),
		*call_info->arguments,
		member_bindings,
		context,
		"Invalid parameter node in bound constexpr member function",
		&bindings);
	if (!bind_result.success()) {
		return bind_result;
	}
	std::vector<std::string_view> pointer_target_writebacks;
	std::vector<std::pair<std::string_view, EvalResult>> pointer_target_imports;
	for (const auto& [param_name, param_value] : member_bindings) {
		(void)param_name;
		if (!param_value.pointer_to_var.isValid()) {
			continue;
		}
		std::string_view target_name = StringTable::getStringView(param_value.pointer_to_var);
		if (member_bindings.find(target_name) != member_bindings.end()) {
			continue;
		}
		const EvalResult* outer_target = findBindingValue(target_name, bindings, context);
		if (!outer_target) {
			continue;
		}
		pointer_target_imports.push_back({target_name, *outer_target});
		pointer_target_writebacks.push_back(target_name);
	}
	for (const auto& [target_name, target_value] : pointer_target_imports) {
		member_bindings[target_name] = target_value;
	}

	// Inject a synthetic "this" binding so that member function bodies can access
	// members by unqualified name (e.g. `return a + b;`).  EvalResult requires a
	// concrete value variant; from_int(0LL) is used as an inert placeholder — the
	// actual data lives in object_type_index and object_member_bindings.
	EvalResult this_binding = EvalResult::from_int(0LL);
	this_binding.object_type_index = bound_type_index;
	this_binding.object_member_bindings = member_bindings;
	if (const TypeInfo* bound_type = tryGetTypeInfo(bound_type_index)) {
		TypeSpecifierNode this_type(
			bound_type->type_index_.withCategory(TypeCategory::Struct),
			bound_type->sizeInBits(),
			Token{},
			context.current_member_function_is_const ? CVQualifier::Const : CVQualifier::None,
			ReferenceQualifier::None);
		this_binding.set_exact_type(this_type);
	}
	member_bindings["this"] = std::move(this_binding);

	auto saved_template_param_names = context.template_param_names;
	auto saved_template_args = context.template_args;
	auto saved_template_environment = context.template_environment;
	auto restore_template_bindings = [&]() {
		context.template_param_names = std::move(saved_template_param_names);
		context.template_args = std::move(saved_template_args);
		context.template_environment = std::move(saved_template_environment);
	};

	if (actual_func->has_outer_template_bindings()) {
		context.template_param_names.clear();
		context.template_args.clear();
		context.template_param_names.reserve(actual_func->outer_template_param_names().size());
		context.template_args.reserve(actual_func->outer_template_args().size());
		for (StringHandle param_name : actual_func->outer_template_param_names()) {
			context.template_param_names.push_back(StringTable::getStringView(param_name));
		}
		for (const auto& arg : actual_func->outer_template_args()) {
			context.template_args.push_back(toTemplateTypeArg(arg));
		}
		context.template_environment = buildOuterFunctionTemplateEnvironment(
			actual_func->outer_template_param_names(),
			actual_func->outer_template_args());
	} else if (receiver_is_this) {
		try_load_current_struct_template_bindings(context);
	} else {
		load_template_bindings_from_type(bound_type_info, context);
	}

	if (context.current_depth >= context.max_recursion_depth) {
		restore_template_bindings();
		return EvalResult::error("Constexpr recursion depth limit exceeded in bound member function call");
	}

	auto saved_struct_info = context.struct_info;
	auto saved_struct_type_index = context.struct_type_index;
	bool saved_current_member_function_is_const = context.current_member_function_is_const;
	context.struct_info = bound_struct_info;
	context.struct_type_index = bound_type_index;
	context.current_member_function_is_const = actual_func->is_const_member_function();
	// Set return_type_info so that aggregate-initializer returns (return {x, y}) work correctly.
	const TypeInfo* saved_return_type_info = context.return_type_info;
	context.return_type_info = nullptr;
	{
		const TypeSpecifierNode& ret_spec = actual_func->decl_node().type_specifier_node();
		TypeIndex ret_idx = ret_spec.type_index();
		if (const TypeInfo* return_type_info = tryGetTypeInfo(ret_idx))
			context.return_type_info = return_type_info;
	}
	context.current_depth++;
	auto* saved_local_bindings = context.local_bindings;
	context.local_bindings = &member_bindings;
	auto result = evaluate_block_with_bindings(
		definition.value(),
		member_bindings,
		context,
		"Member function body is not a block",
		"Constexpr member function did not return a value");
	if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
		FLASH_LOG(ConstExpr, Debug,
			"  bound member body result success=", result.success(),
			" int=", result.success() ? result.as_int() : 0,
			" members=", member_bindings.size());
	}
	context.local_bindings = saved_local_bindings;
	context.current_depth--;
	context.return_type_info = saved_return_type_info;
	context.struct_info = saved_struct_info;
	context.struct_type_index = saved_struct_type_index;
	context.current_member_function_is_const = saved_current_member_function_is_const;
	if (!result.success() && (result.error_message == "Constexpr member function did not return a value" ||
							  result.error_message == "Constexpr function return statement has no expression")) {
		{
			const TypeSpecifierNode& ret_spec = actual_func->decl_node().type_specifier_node();
			if (ret_spec.category() == TypeCategory::Void) {
				result = EvalResult::from_int(0LL);
			}
		}
	}
	if (result.success() && mutable_bindings) {
		// Remove the synthetic "this" injected above before writing member state
		// back to the outer map, so "this" is not accidentally stored as a member.
		member_bindings.erase("this");
		for (std::string_view target_name : pointer_target_writebacks) {
			auto local_it = member_bindings.find(target_name);
			if (local_it == member_bindings.end()) {
				continue;
			}
			if (EvalResult* outer_target = findMutableBindingValue(target_name, *mutable_bindings, context)) {
				*outer_target = local_it->second;
			}
		}
		if (write_back_to_object_binding) {
			if (write_back_via_receiver_lvalue) {
				EvalResult updated_object = pointed_object_result;
				updated_object.object_member_bindings = member_bindings;
				EvalResult write_back_result = write_value_to_bound_lvalue(
					call_info->receiver,
					updated_object,
					*mutable_bindings,
					context);
				if (!write_back_result.success()) {
					result = write_back_result;
				}
			} else {
				auto object_it = mutable_bindings->find(object_name);
				if (object_it != mutable_bindings->end()) {
					object_it->second.object_member_bindings = member_bindings;
				}
			}
		} else if (write_back_through_pointer) {
			EvalResult updated_object = pointed_object_result;
			updated_object.object_member_bindings = member_bindings;
			Token deref_token(
				Token::Type::Operator,
				"*"sv,
				call_info->called_from.line(),
				call_info->called_from.column(),
				call_info->called_from.file_index());
			ASTNode deref_expr = ASTNode::emplace_node<ExpressionNode>(
				UnaryOperatorNode(deref_token, call_info->receiver, /* is_prefix= */ true, /* is_builtin_addressof= */ false));
			EvalResult write_back_result = write_value_to_bound_lvalue(
				deref_expr,
				updated_object,
				*mutable_bindings,
				context);
			if (!write_back_result.success()) {
				result = write_back_result;
			}
		} else if (receiver_is_this) {
			// When the receiver is `this`, write each mutated struct member back into
			// the outer mutable_bindings map (which holds this's members as direct
			// keys).  Only applies to this-receiver calls — non-this complex receivers
			// use write_back_to_object_binding or write_back_through_pointer instead.
			for (const auto& member : saved_struct_info->members) {
				std::string_view member_name = StringTable::getStringView(member.getName());
				auto member_it = member_bindings.find(member_name);
				if (member_it != member_bindings.end()) {
					(*mutable_bindings)[member_name] = member_it->second;
				}
			}
		}
	}
	restore_template_bindings();
	return result;
}

EvalResult Evaluator::call_constexpr_member_fn_on_object(
	const EvalResult& object,
	std::string_view func_name,
	EvaluationContext& context) {
	const TypeInfo* type_info = tryGetTypeInfo(object.object_type_index);
	if (!type_info)
		return EvalResult::error("Object has no valid type info for member function '" + std::string(func_name) + "'");

	const StructTypeInfo* struct_info = type_info->getStructInfo();
	if (!struct_info)
		return EvalResult::error("Object is not a struct type for member function '" + std::string(func_name) + "'");

	StringHandle func_name_handle = StringTable::getOrInternStringHandle(func_name);
	const bool object_is_const =
		object.exact_type.has_value() && object.exact_type->is_const();
	auto findConstAwareMemberCandidateForStruct =
		[&](const StructTypeInfo* target_struct, bool detect_ambiguity) -> ResolvedMemberFunctionCandidate {
			return findConstAwareMemberFunctionCandidate(
				target_struct,
				func_name_handle,
				0,
				context,
				MemberFunctionLookupMode::LookupOnly,
				false,
				object_is_const,
				detect_ambiguity);
		};

	// Try instantiation's own member functions first, then fall back to the
	// base template (same logic as find_current_struct_member_function_candidate).
	auto match = findConstAwareMemberCandidateForStruct(struct_info, true);

	if ((!match.function && !match.ambiguous) ||
		(match.function && !match.function->is_materialized())) {
		// Try the base template's struct info for template instantiations.
		auto struct_type_it = getTypesByNameMap().find(struct_info->name);
		if (struct_type_it != getTypesByNameMap().end() && struct_type_it->second->isTemplateInstantiation()) {
			const TypeInfo* struct_type = struct_type_it->second;
			auto template_type_it = getTypesByNameMap().find(struct_type->baseTemplateName());
			if (template_type_it != getTypesByNameMap().end() && template_type_it->second->isStruct()) {
				match = findConstAwareMemberCandidateForStruct(
					template_type_it->second->getStructInfo(),
					false);
			}
		}
	}

	if (match.ambiguous)
		return EvalResult::error("Ambiguous member function '" + std::string(func_name) + "' in constexpr range-based for");
	if (!match.function)
		return EvalResult::error("Member function '" + std::string(func_name) + "' not found");
	if (!match.function->is_constexpr() && !match.function->is_consteval())
		return EvalResult::error("Member function '" + std::string(func_name) + "' is not constexpr");

	const auto& def = match.function->get_definition();
	if (!def.has_value())
		return EvalResult::error("Member function '" + std::string(func_name) + "' has no body");

	if (context.current_depth >= context.max_recursion_depth)
		return EvalResult::error("Constexpr recursion depth limit exceeded in call to '" + std::string(func_name) + "'");

	auto member_bindings = object.object_member_bindings;
	EvalResult this_binding = EvalResult::from_int(0LL);
	this_binding.object_type_index = object.object_type_index;
	this_binding.object_member_bindings = object.object_member_bindings;
	this_binding.exact_type = object.exact_type;
	member_bindings["this"] = std::move(this_binding);

	// Load template type bindings, preferring function-level outer bindings when present.
	auto saved_template_param_names = context.template_param_names;
	auto saved_template_args = context.template_args;
	auto saved_template_environment = context.template_environment;
	if (match.function->has_outer_template_bindings()) {
		context.template_param_names.clear();
		context.template_args.clear();
		context.template_param_names.reserve(match.function->outer_template_param_names().size());
		context.template_args.reserve(match.function->outer_template_args().size());
		for (StringHandle param_name : match.function->outer_template_param_names())
			context.template_param_names.push_back(StringTable::getStringView(param_name));
		for (const auto& arg : match.function->outer_template_args())
			context.template_args.push_back(toTemplateTypeArg(arg));
		context.template_environment = buildOuterFunctionTemplateEnvironment(
			match.function->outer_template_param_names(),
			match.function->outer_template_args());
	} else {
		load_template_bindings_from_type(type_info, context);
	}

	auto saved_struct_info = context.struct_info;
	auto saved_struct_type_index = context.struct_type_index;
	const TypeInfo* saved_return_type_info = context.return_type_info;
	auto* saved_local_bindings = context.local_bindings;
	bool saved_current_member_function_is_const = context.current_member_function_is_const;
	context.struct_info = struct_info;
	context.struct_type_index = object.object_type_index;
	context.return_type_info = nullptr;
	context.local_bindings = &member_bindings;
	context.current_member_function_is_const = match.function->is_const_member_function();
	{
		const TypeSpecifierNode& ret_spec = match.function->decl_node().type_specifier_node();
		TypeIndex ret_idx = ret_spec.type_index();
		if (const TypeInfo* return_type_info = tryGetTypeInfo(ret_idx))
			context.return_type_info = return_type_info;
	}
	context.current_depth++;

	auto result = evaluate_block_with_bindings(
		def.value(),
		member_bindings,
		context,
		"Member function body is not a block",
		"Constexpr member function did not return a value");

	context.current_depth--;
	context.local_bindings = saved_local_bindings;
	context.return_type_info = saved_return_type_info;
	context.struct_info = saved_struct_info;
	context.struct_type_index = saved_struct_type_index;
	context.current_member_function_is_const = saved_current_member_function_is_const;
	context.template_param_names = std::move(saved_template_param_names);
	context.template_args = std::move(saved_template_args);
	context.template_environment = std::move(saved_template_environment);
	return result;
}

Evaluator::ResolvedMemberFunctionCandidate Evaluator::find_call_operator_candidate(
	const StructTypeInfo* struct_info,
	size_t argument_count,
	bool detect_ambiguity) {
	ResolvedMemberFunctionCandidate result;
	if (!struct_info) {
		return result;
	}

	for (const auto& member_func : struct_info->member_functions) {
		if (member_func.is_constructor || member_func.is_destructor) {
			continue;
		}
		if (member_func.operator_kind != OverloadableOperator::Call) {
			continue;
		}
		if (!member_func.function_decl.is<FunctionDeclarationNode>()) {
			continue;
		}

		const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
		const size_t parameter_count = func_decl.parameter_nodes().size();
		const size_t min_required = countMinRequiredArgs(func_decl);
		if (argument_count < min_required || argument_count > parameter_count) {
			continue;
		}

		if (result.function && detect_ambiguity) {
			result.ambiguous = true;
			return result;
		}

		result.function = &func_decl;
		result.member = &member_func;
		if (!detect_ambiguity) {
			break;
		}
	}

	return result;
}

Evaluator::ResolvedMemberFunctionCandidate Evaluator::find_member_function_candidate(
	const StructTypeInfo* struct_info,
	StringHandle function_name_handle,
	size_t argument_count,
	EvaluationContext& context,
	MemberFunctionLookupMode lookup_mode,
	bool require_static,
	bool detect_ambiguity) {
	ResolvedMemberFunctionCandidate result;
	if (!struct_info) {
		return result;
	}

	for (const auto& member_func : struct_info->member_functions) {
		if (member_func.is_constructor || member_func.is_destructor ||
			!matchesConstexprFunctionName(member_func, function_name_handle) ||
			!member_func.function_decl.is<FunctionDeclarationNode>()) {
			continue;
		}

		const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
		if (require_static && !func_decl.is_static()) {
			continue;
		}
		if (!isConstexprMemberLookupCandidate(
				func_decl,
				argument_count,
				context,
				lookup_mode,
				require_static)) {
			continue;
		}

		if (result.function && detect_ambiguity) {
			result.ambiguous = true;
			return result;
		}

		result.function = &func_decl;
		result.member = &member_func;
		if (!detect_ambiguity) {
			break;
		}
	}

	return result;
}

Evaluator::ResolvedMemberFunctionCandidate Evaluator::findConstAwareMemberFunctionCandidate(
	const StructTypeInfo* struct_info,
	StringHandle function_name_handle,
	size_t argument_count,
	EvaluationContext& context,
	MemberFunctionLookupMode lookup_mode,
	bool require_static,
	bool receiver_is_const,
	bool detect_ambiguity) {
	if (require_static) {
		return find_member_function_candidate(
			struct_info,
			function_name_handle,
			argument_count,
			context,
			lookup_mode,
			true,
			detect_ambiguity);
	}
	const ConstAwareMemberCandidateSet candidate_sets =
		collectConstAwareVisibleMemberFunctionCandidates(
			struct_info,
			function_name_handle,
			receiver_is_const,
			false,
			[&](const StructMemberFunction& member_func, const FunctionDeclarationNode& func_decl) {
				return matchesConstexprFunctionName(member_func, function_name_handle) &&
					isConstexprMemberLookupCandidate(
						func_decl,
						argument_count,
						context,
						lookup_mode,
						false);
			});

	auto selectUniqueCandidate =
		[&](std::span<const StructMemberFunction* const> members) -> ResolvedMemberFunctionCandidate {
			ResolvedMemberFunctionCandidate result;
			if (members.empty()) {
				return result;
			}
			result.member = members.front();
			result.function =
				result.member != nullptr &&
					result.member->function_decl.is<FunctionDeclarationNode>()
					? &result.member->function_decl.as<FunctionDeclarationNode>()
					: nullptr;
			if (detect_ambiguity && members.size() > 1) {
				result.ambiguous = true;
			}
			return result;
		};

	ResolvedMemberFunctionCandidate result =
		selectUniqueCandidate(candidate_sets.preferred);
	if (result.function != nullptr || result.ambiguous) {
		return result;
	}
	if (!sameMemberCandidateSet(candidate_sets.preferred, candidate_sets.compatible)) {
		result = selectUniqueCandidate(candidate_sets.compatible);
	}
	return result;
}

Evaluator::ResolvedMemberFunctionCandidate Evaluator::findConstexprOperatorOverload(
	const StructTypeInfo* struct_info,
	StringHandle operator_name,
	size_t argument_count,
	EvaluationContext& context) {
	auto candidate = find_member_function_candidate(
		struct_info, operator_name, argument_count, context,
		MemberFunctionLookupMode::ConstexprEvaluable, false, true);
	if (!candidate.ambiguous && candidate.function && candidate.function->get_definition().has_value()) {
		return candidate;
	}
	if (!candidate.ambiguous) {
		return {};
	}
	auto sameTypeSpecifierShape = [](const TypeSpecifierNode& lhs, const TypeSpecifierNode& rhs) {
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
	};
	auto sameParameterTypes = [&](const FunctionDeclarationNode& lhs, const FunctionDeclarationNode& rhs) {
		const auto& lhs_params = lhs.parameter_nodes();
		const auto& rhs_params = rhs.parameter_nodes();
		if (lhs_params.size() != rhs_params.size()) {
			return false;
		}
		for (size_t i = 0; i < lhs_params.size(); ++i) {
			if (!lhs_params[i].is<DeclarationNode>() || !rhs_params[i].is<DeclarationNode>()) {
				return false;
			}
			const auto& lhs_decl = lhs_params[i].as<DeclarationNode>();
			const auto& rhs_decl = rhs_params[i].as<DeclarationNode>();
			if (!sameTypeSpecifierShape(lhs_decl.type_specifier_node(), rhs_decl.type_specifier_node())) {
				return false;
			}
		}
		return true;
	};
	std::vector<const FunctionDeclarationNode*> viable_candidates;
	for (const auto& member_func : struct_info->member_functions) {
		if (member_func.is_constructor || member_func.is_destructor ||
			member_func.getName() != operator_name ||
			!member_func.function_decl.is<FunctionDeclarationNode>()) {
			continue;
		}
		const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
		if (!isConstexprMemberLookupCandidate(
				func_decl, argument_count, context,
				MemberFunctionLookupMode::ConstexprEvaluable, false)) {
			continue;
		}
		if (!func_decl.get_definition().has_value()) {
			continue;
		}
		viable_candidates.push_back(&func_decl);
	}
	if (viable_candidates.empty()) {
		return {};
	}
	if (viable_candidates.size() == 1) {
		return {.function = viable_candidates.front(), .ambiguous = false};
	}
	for (size_t i = 1; i < viable_candidates.size(); ++i) {
		if (!sameParameterTypes(*viable_candidates[0], *viable_candidates[i])) {
			return {};
		}
	}
	const FunctionDeclarationNode* const_overload = nullptr;
	for (const FunctionDeclarationNode* viable_candidate : viable_candidates) {
		if (!viable_candidate->is_const_member_function()) {
			continue;
		}
		if (const_overload) {
			return {.function = nullptr, .ambiguous = true};
		}
		const_overload = viable_candidate;
	}
	if (const_overload) {
		return {.function = const_overload, .ambiguous = false};
	}
	return {.function = nullptr, .ambiguous = true};
}

EvalResult Evaluator::invokeConstexprMemberFunction(
	const FunctionDeclarationNode& func,
	std::unordered_map<std::string_view, EvalResult> member_bindings,
	TypeIndex type_index,
	const TypeInfo* type_info,
	const StructTypeInfo* struct_info,
	std::vector<EvalResult> pre_evaluated_args,
	EvaluationContext& context,
	std::string_view body_error,
	std::string_view return_error) {
	EvalResult this_binding = EvalResult::from_int(0LL);
	this_binding.object_type_index = type_index;
	this_binding.object_member_bindings = member_bindings;
	member_bindings["this"] = std::move(this_binding);
	auto bind_result = bind_pre_evaluated_arguments(
		func.parameter_nodes(),
		pre_evaluated_args,
		member_bindings,
		context,
		"Invalid parameter node in constexpr member function call",
		false);
	if (!bind_result.success()) {
		return bind_result;
	}
	auto saved_template_param_names = context.template_param_names;
	auto saved_template_args = context.template_args;
	auto saved_template_environment = context.template_environment;
	if (func.has_outer_template_bindings()) {
		context.template_param_names.clear();
		context.template_args.clear();
		context.template_param_names.reserve(func.outer_template_param_names().size());
		context.template_args.reserve(func.outer_template_args().size());
		for (StringHandle param_name : func.outer_template_param_names()) {
			context.template_param_names.push_back(StringTable::getStringView(param_name));
		}
		for (const auto& arg : func.outer_template_args()) {
			context.template_args.push_back(toTemplateTypeArg(arg));
		}
		context.template_environment = buildOuterFunctionTemplateEnvironment(
			func.outer_template_param_names(),
			func.outer_template_args());
	} else {
		load_template_bindings_from_type(type_info, context);
	}
	if (context.current_depth >= context.max_recursion_depth) {
		context.template_param_names = std::move(saved_template_param_names);
		context.template_args = std::move(saved_template_args);
		context.template_environment = std::move(saved_template_environment);
		return EvalResult::error("Constexpr recursion depth limit exceeded in member function call");
	}
	auto saved_struct_info = context.struct_info;
	auto saved_struct_type_index = context.struct_type_index;
	const TypeInfo* saved_return_type_info = context.return_type_info;
	context.struct_info = struct_info;
	context.struct_type_index = type_index;
	context.return_type_info = nullptr;
	{
		const TypeSpecifierNode& ret_spec = func.decl_node().type_specifier_node();
		TypeIndex ret_idx = ret_spec.type_index();
		if (const TypeInfo* rtype = tryGetTypeInfo(ret_idx))
			context.return_type_info = rtype;
	}
	context.current_depth++;
	auto* saved_local_bindings = context.local_bindings;
	context.local_bindings = &member_bindings;
	auto result = evaluate_block_with_bindings(
		func.get_definition().value(),
		member_bindings,
		context,
		body_error,
		return_error);
	context.local_bindings = saved_local_bindings;
	context.current_depth--;
	context.return_type_info = saved_return_type_info;
	context.struct_info = saved_struct_info;
	context.struct_type_index = saved_struct_type_index;
	context.template_param_names = std::move(saved_template_param_names);
	context.template_args = std::move(saved_template_args);
	context.template_environment = std::move(saved_template_environment);
	return result;
}

Evaluator::ResolvedMemberFunctionCandidate Evaluator::find_current_struct_member_function_candidate(
	StringHandle function_name_handle,
	size_t argument_count,
	EvaluationContext& context,
	MemberFunctionLookupMode lookup_mode,
	bool require_static,
	bool detect_ambiguity_in_current_struct) {
	ResolvedMemberFunctionCandidate result;
	if (!context.struct_info) {
		return result;
	}

	// Phase 5 Slice D: route lazy member-function materialization through the
	// sema-owned helper so the evaluator no longer drives the
	// instantiate/normalize/mark bookkeeping directly.
	context
		.requireParserAttachedSema(kMemberFunctionMaterializationLookupOp)
		.parserSemanticServices()
		.ensureMemberFunctionMaterialized(
			context.struct_info->name, function_name_handle, std::nullopt);

	auto findConstAwareCurrentStructCandidate =
		[&](const StructTypeInfo* target_struct, bool detect_ambiguity) -> ResolvedMemberFunctionCandidate {
			return findConstAwareMemberFunctionCandidate(
				target_struct,
				function_name_handle,
				argument_count,
				context,
				lookup_mode,
				require_static,
				context.current_member_function_is_const,
				detect_ambiguity);
		};

	result = findConstAwareCurrentStructCandidate(
		context.struct_info,
		detect_ambiguity_in_current_struct);
	if (result.function && !result.function->is_materialized()) {
		StringHandle owner_name = context.struct_info->name;
		if (!result.function->parent_struct_name().empty()) {
			owner_name = StringTable::getOrInternStringHandle(result.function->parent_struct_name());
		}
		context
			.requireParserAttachedSema(kMemberFunctionMaterializationReplayOp)
			.parserSemanticServices()
			.ensureMemberFunctionMaterialized(owner_name, *result.function);
		result = findConstAwareCurrentStructCandidate(
			context.struct_info,
			detect_ambiguity_in_current_struct);
	}
	if (result.function || result.ambiguous) {
		return result;
	}

	auto struct_type_it = getTypesByNameMap().find(context.struct_info->name);
	if (struct_type_it != getTypesByNameMap().end() && struct_type_it->second->isTemplateInstantiation()) {
		const TypeInfo* struct_type = struct_type_it->second;
		auto template_type_it = getTypesByNameMap().find(struct_type->baseTemplateName());
		if (template_type_it != getTypesByNameMap().end() && template_type_it->second->isStruct()) {
			return findConstAwareCurrentStructCandidate(
				template_type_it->second->getStructInfo(),
				false);
		}
	}

	return result;
}

std::optional<StringHandle> Evaluator::get_current_struct_static_lookup_name_handle(
	const IdentifierNode* identifier,
	CurrentStructStaticLookupMode lookup_mode) {
	if (!identifier) {
		return std::nullopt;
	}

	bool should_try_current_struct = false;
	switch (lookup_mode) {
	case CurrentStructStaticLookupMode::BoundOnly:
		should_try_current_struct = identifier->binding() == IdentifierBinding::StaticMember;
		break;
	case CurrentStructStaticLookupMode::PreferCurrentStruct:
		should_try_current_struct =
			identifier->binding() == IdentifierBinding::StaticMember ||
			identifier->binding() == IdentifierBinding::Global ||
			identifier->binding() == IdentifierBinding::Unresolved;
		break;
	}

	if (!should_try_current_struct) {
		return std::nullopt;
	}

	StringHandle member_name_handle = identifier->nameHandle();
	if (!member_name_handle.isValid()) {
		member_name_handle = StringTable::getOrInternStringHandle(identifier->name());
	}

	return member_name_handle;
}

Evaluator::ResolvedCurrentStructStaticMember Evaluator::resolve_current_struct_static_member(
	const IdentifierNode* identifier,
	const EvaluationContext& context,
	CurrentStructStaticLookupMode lookup_mode) {
	if (!identifier || context.struct_info == nullptr) {
		return {};
	}

	auto member_name_handle = get_current_struct_static_lookup_name_handle(identifier, lookup_mode);
	if (!member_name_handle.has_value()) {
		return {};
	}

	auto [static_member, owner_struct] = context.struct_info->findStaticMemberRecursive(member_name_handle.value());
	return {static_member, owner_struct};
}

Evaluator::ResolvedCurrentStructStaticInitializer Evaluator::resolve_current_struct_static_initializer(
	const IdentifierNode* identifier,
	const EvaluationContext& context,
	CurrentStructStaticLookupMode lookup_mode) {
	if (!identifier) {
		return {};
	}

	if (auto static_member_result = resolve_current_struct_static_member(identifier, context, lookup_mode);
		static_member_result.static_member) {
		return {&static_member_result.static_member->initializer, true, static_member_result.static_member};
	}

	auto member_name_handle = get_current_struct_static_lookup_name_handle(identifier, lookup_mode);
	if (!member_name_handle.has_value() || context.struct_node == nullptr) {
		return {};
	}

	for (const auto& static_member : context.struct_node->static_members()) {
		if (static_member.name == member_name_handle.value()) {
			return {&static_member.initializer, true};
		}
	}

	return {};
}

EvalResult Evaluator::evaluate_expression_with_bindings(
	const ASTNode& expr_node,
	std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context) {

	if (!expr_node.is<ExpressionNode>()) {
		return EvalResult::error("Not an expression node");
	}

	const ExpressionNode& expr = expr_node.as<ExpressionNode>();

	if (const auto* lambda_expression = std::get_if<LambdaExpressionNode>(&expr)) {
		return materialize_lambda_value(*lambda_expression, context, &bindings);
	}

	if (const auto* sizeof_expr = std::get_if<SizeofExprNode>(&expr)) {
		auto* saved_local_bindings = context.local_bindings;
		context.local_bindings = &bindings;
		auto restore = ScopeGuard([&context, saved_local_bindings]() {
			context.local_bindings = saved_local_bindings;
		});
		return evaluate_sizeof(*sizeof_expr, context);
	}

	if (const auto* alignof_expr = std::get_if<AlignofExprNode>(&expr)) {
		auto* saved_local_bindings = context.local_bindings;
		context.local_bindings = &bindings;
		auto restore = ScopeGuard([&context, saved_local_bindings]() {
			context.local_bindings = saved_local_bindings;
		});
		return evaluate_alignof(*alignof_expr, context);
	}

	// Check if it's an identifier that matches a parameter
	if (std::holds_alternative<IdentifierNode>(expr)) {
		const IdentifierNode& id = std::get<IdentifierNode>(expr);

		// Fast path: pre-resolved Local bindings are always in the bindings map
		if (id.binding() == IdentifierBinding::Local) {
			if (const EvalResult* bound_value = findBindingValue(id.name(), bindings, context)) {
				return read_bound_identifier_value(*bound_value, id.name(), bindings, context);
			}
			// fall through to existing logic as safety net
		}

		std::string_view name = id.name();

		// Check if it's a bound parameter
		if (const EvalResult* bound_value = findBindingValue(name, bindings, context)) {
			return read_bound_identifier_value(*bound_value, name, bindings, context);
		}

		// Not a parameter, evaluate normally
		return evaluate_identifier(id, context);
	}

	if (auto member_result = try_evaluate_bound_member_access(expr, bindings, context)) {
		return *member_result;
	}

	if (auto array_result = try_evaluate_bound_array_subscript(expr, bindings, context)) {
		return *array_result;
	}

	// For binary operators, recursively evaluate with bindings
	if (std::holds_alternative<BinaryOperatorNode>(expr)) {
		const auto& bin_op = std::get<BinaryOperatorNode>(expr);
		std::string_view op = bin_op.op();

		// Handle assignment operators specially (they modify bindings)
		if (op == "=" || isCompoundAssignmentOp(op)) {

			// Helper: apply the assignment/compound-assignment operator to a target slot.
			// Modifies `target` in place and returns the resulting value.
			auto apply_op_to = [&](EvalResult& target, const EvalResult& rhs) -> EvalResult {
				if (op == "=") {
					target = rhs;
					return rhs;
				}
				// Compound assignment reads the current value — reject indeterminate targets.
				if (target.is_indeterminate) {
					return EvalResult::error(
						"Read of indeterminate value in constant expression "
						"(object was default-initialized without an initializer)");
				}
				// Strip the trailing '=' to get the base operator (e.g., "+=" → "+")
				std::string_view base_op = op.substr(0, op.size() - 1);
				EvalResult new_val = apply_binary_op(target, rhs, base_op, &context, &bindings);
				if (!new_val.success())
					return new_val;
				// Apply unsigned type-width truncation to match C++ assignment-conversion
				// semantics. For unsigned integer types, the stored result must wrap at
				// the declared type's width, regardless of any integer promotion during
				// binary arithmetic. apply_uint_type_mask handles the 64-bit case by
				// returning the value unmodified (masking with all-ones is a no-op).
				if (target.exact_type.has_value() &&
					is_unsigned_integer_type(target.exact_type->category())) {
					new_val = EvalResult::from_uint(apply_uint_type_mask(new_val.as_uint_raw(), target.exact_type));
					new_val.set_exact_type(*target.exact_type);
				}
				target = new_val;
				return new_val;
			};
			// Get the left-hand side variable name
			const ASTNode& lhs = bin_op.get_lhs();
			if (const auto* member_lhs = tryGetNode<MemberAccessNode>(lhs)) {
				const IdentifierNode* direct_object_id = tryGetIdentifier(member_lhs->object());
				if (!direct_object_id || direct_object_id->name() != "this") {
					std::optional<EvalResult> resolve_error;
					if (std::optional<BoundWriteTarget> target = resolveBoundWriteTarget(
							lhs, bindings, context, evaluate_expression_with_bindings_const, resolve_error);
						target.has_value() && target->slot != nullptr) {
						auto rhs_result = evaluate_expression_with_bindings(bin_op.get_rhs(), bindings, context);
						if (!rhs_result.success()) {
							return rhs_result;
						}
						auto assign_result = apply_op_to(*target->slot, rhs_result);
						if (assign_result.success() && !target->root_name.empty()) {
							if (EvalResult* root_binding = findMutableBindingValue(target->root_name, bindings, context)) {
								refreshPointerSnapshotsForBinding(target->root_name, *root_binding, bindings, context);
							}
						}
						return assign_result;
					}
					if (resolve_error.has_value()) {
						return *resolve_error;
					}
				}
			}
			if (lhs.is<ExpressionNode>()) {
				const ExpressionNode& lhs_expr = lhs.as<ExpressionNode>();

				// Determine the name of the variable being assigned to.
				// Two forms are accepted:
				//   1. Plain identifier:     x = ...
				//   2. this->member access:  this->x = ...
				std::string_view var_name;
				bool assign_to_member_binding = false;
				if (const auto* identifier_ptr = std::get_if<IdentifierNode>(&lhs_expr)) {
					var_name = identifier_ptr->name();
				} else if (std::holds_alternative<MemberAccessNode>(lhs_expr)) {
					const auto& ma = std::get<MemberAccessNode>(lhs_expr);
					const ASTNode& obj = ma.object();
					if (obj.is<ExpressionNode>()) {
						const ExpressionNode& obj_expr = obj.as<ExpressionNode>();
						if (std::holds_alternative<IdentifierNode>(obj_expr) &&
							std::get<IdentifierNode>(obj_expr).name() == "this") {
							var_name = ma.member_name();
							assign_to_member_binding = true;
						}
					}
				}

				if (!var_name.empty()) {
					// Evaluate the right-hand side
					auto rhs_result = evaluate_expression_with_bindings(bin_op.get_rhs(), bindings, context);
					if (!rhs_result.success())
						return rhs_result;

					EvalResult* target_binding = nullptr;
					if (assign_to_member_binding) {
						auto member_it = bindings.find(var_name);
						if (member_it != bindings.end()) {
							target_binding = &member_it->second;
						}
					} else {
						target_binding = findMutableBindingValue(var_name, bindings, context);
					}
					std::string_view target_binding_name = var_name;
					if (target_binding && !assign_to_member_binding) {
						std::optional<EvalResult> resolve_error;
						if (!resolveMutableReferenceAliasTarget(
								target_binding,
								target_binding_name,
								bindings,
								context,
								var_name,
								resolve_error)) {
							return resolve_error.has_value()
								? *resolve_error
								: EvalResult::error("Failed to resolve reference alias assignment target");
						}
					}

					// Perform the assignment
					if (op == "=") {
						if (target_binding) {
							*target_binding = rhs_result;
							refreshPointerSnapshotsForBinding(target_binding_name, *target_binding, bindings, context);
						} else {
							bindings[var_name] = rhs_result;
							refreshPointerSnapshotsForBinding(var_name, bindings[var_name], bindings, context);
						}
						return rhs_result;
					} else {
						// Compound assignment - use apply_op_to helper
						if (!target_binding) {
							return EvalResult::error("Variable not found for compound assignment: " + std::string(var_name));
						}
						auto result = apply_op_to(*target_binding, rhs_result);
						if (result.success()) {
							refreshPointerSnapshotsForBinding(target_binding_name, *target_binding, bindings, context);
						}
						return result;
					}
				}

				// Handle pointer dereference assignment: *ptr = value  (C++20 constexpr heap)
				// LHS is UnaryOperatorNode(op="*", operand=expr_that_yields_pointer)
				if (const auto* unary_ptr = std::get_if<UnaryOperatorNode>(&lhs_expr)) {
					if (unary_ptr->op() == "*") {
						auto ptr_result = evaluate_expression_with_bindings(
							unary_ptr->get_operand(), bindings, context);
						if (!ptr_result.success())
							return ptr_result;
						if (!ptr_result.pointer_to_var.isValid()) {
							return EvalResult::error("Dereference assignment on non-pointer in constant expression");
						}
						StringHandle heap_key = ptr_result.pointer_to_var;
						auto heap_it = context.constexpr_heap.find(heap_key);
						if (heap_it == context.constexpr_heap.end()) {
							// Not a heap allocation — check if the pointer targets a local
							// binding in the current frame (e.g., `int x; int* p = &x; *p = 5`).
							std::string_view local_name = heap_key.view();
							auto local_it = bindings.find(local_name);
							if (local_it != bindings.end()) {
								if (ptr_result.pointer_offset != 0) {
									return EvalResult::error(
										"Non-zero pointer offset on non-array local variable '"
										+ std::string(local_name) + "' in constexpr dereference assignment");
								}
								// Evaluate the RHS before mutating.
								auto rhs_result = evaluate_expression_with_bindings(bin_op.get_rhs(), bindings, context);
								if (!rhs_result.success())
									return rhs_result;
								auto assign_result = apply_op_to(local_it->second, rhs_result);
								// Keep any other same-frame pointer snapshots in sync.
								if (assign_result.success())
									refreshPointerSnapshotsForBinding(local_name, local_it->second, bindings, context);
								return assign_result;
							}
							return EvalResult::error("Dereference assignment: pointer target is neither a heap allocation nor a local binding in constant expression");
						}
						if (heap_it->second.freed) {
							return EvalResult::error("Dereference assignment: use after free in constant expression");
						}
						int64_t offset = ptr_result.pointer_offset;

						// Evaluate the RHS
						auto rhs_result = evaluate_expression_with_bindings(bin_op.get_rhs(), bindings, context);
						if (!rhs_result.success())
							return rhs_result;

						EvalResult& heap_val = heap_it->second.value;
						if (heap_val.is_array) {
							// Pointer into an array: update the element at offset
							if (offset < 0 || static_cast<size_t>(offset) >= heap_val.array_elements.size()) {
								return EvalResult::error("Array index out of bounds in constexpr dereference assignment");
							}
							return apply_op_to(heap_val.array_elements[static_cast<size_t>(offset)], rhs_result);
						} else {
							if (offset != 0) {
								return EvalResult::error("Non-zero pointer offset on non-array heap object in constexpr assignment");
							}
							return apply_op_to(heap_val, rhs_result);
						}
					}
				}

				// Handle arrow member assignment: p->member = value  (C++20 constexpr heap)
				// LHS is MemberAccessNode with is_arrow() == true.
				if (std::holds_alternative<MemberAccessNode>(lhs_expr)) {
					const auto& ma = std::get<MemberAccessNode>(lhs_expr);
					if (ma.is_arrow()) {
						// Evaluate the pointer object on the left side
						auto ptr_result = evaluate_expression_with_bindings(ma.object(), bindings, context);
						if (!ptr_result.success())
							return ptr_result;
						if (!ptr_result.pointer_to_var.isValid()) {
							return EvalResult::error("Arrow assignment: left-hand side is not a pointer to a constexpr heap object");
						}
						StringHandle heap_key = ptr_result.pointer_to_var;
						auto heap_it = context.constexpr_heap.find(heap_key);
						if (heap_it == context.constexpr_heap.end()) {
							std::string_view pointed_name = heap_key.view();
							EvalResult* object_binding = findMutableBindingValue(pointed_name, bindings, context);
							if (!object_binding) {
								return EvalResult::error("Arrow assignment: pointer does not refer to a constexpr heap object or local binding");
							}
							EvalResult* object_slot = object_binding;
							if (object_binding->is_array) {
								if (ptr_result.pointer_offset < 0 ||
									static_cast<size_t>(ptr_result.pointer_offset) >= object_binding->array_elements.size()) {
									return EvalResult::error("Arrow assignment: pointer offset out of bounds for local struct array");
								}
								object_slot = &object_binding->array_elements[static_cast<size_t>(ptr_result.pointer_offset)];
							} else if (ptr_result.pointer_offset != 0) {
								return EvalResult::error(
									"Arrow assignment: non-zero pointer offset on non-array local object in constexpr assignment");
							}
							auto member_it = object_slot->object_member_bindings.find(ma.member_name());
							if (member_it == object_slot->object_member_bindings.end()) {
								return EvalResult::error("Arrow assignment: member not found in local struct: " + std::string(ma.member_name()));
							}
							auto rhs_result = evaluate_expression_with_bindings(bin_op.get_rhs(), bindings, context);
							if (!rhs_result.success())
								return rhs_result;
							auto assign_result = apply_op_to(member_it->second, rhs_result);
							if (assign_result.success()) {
								refreshPointerSnapshotsForBinding(pointed_name, *object_binding, bindings, context);
							}
							return assign_result;
						}
						if (heap_it->second.freed) {
							return EvalResult::error("Arrow assignment: use after free in constant expression");
						}
						// Evaluate the RHS
						auto rhs_result = evaluate_expression_with_bindings(bin_op.get_rhs(), bindings, context);
						if (!rhs_result.success())
							return rhs_result;
						// Update the member in the heap object
						auto& heap_val = heap_it->second.value;
						int64_t offset = ptr_result.pointer_offset;
						EvalResult* member_slot = nullptr;
						if (heap_val.is_array) {
							// Arrow on an element of a heap struct array
							if (offset < 0 || static_cast<size_t>(offset) >= heap_val.array_elements.size()) {
								return EvalResult::error("Arrow assignment: pointer offset out of bounds for heap struct array");
							}
							auto& elem = heap_val.array_elements[static_cast<size_t>(offset)];
							auto m_it = elem.object_member_bindings.find(ma.member_name());
							if (m_it == elem.object_member_bindings.end()) {
								return EvalResult::error("Arrow assignment: member not found in heap struct array element: " + std::string(ma.member_name()));
							}
							member_slot = &m_it->second;
						} else {
							if (offset != 0) {
								return EvalResult::error("Arrow assignment: non-zero pointer offset on non-array heap object in constexpr assignment");
							}
							auto m_it = heap_val.object_member_bindings.find(ma.member_name());
							if (m_it == heap_val.object_member_bindings.end()) {
								return EvalResult::error("Arrow assignment: member not found in heap struct: " + std::string(ma.member_name()));
							}
							member_slot = &m_it->second;
						}
						return apply_op_to(*member_slot, rhs_result);
					}
				}

			// Handle dot member assignment: local_obj.member = value (non-this, non-arrow)
			// This handles patterns like: Pair p{3,4}; p.a = 10;
				if (const auto* ma_ptr = std::get_if<MemberAccessNode>(&lhs_expr);
					ma_ptr && !ma_ptr->is_arrow()) {
				// Lambda encapsulates the dot-assignment logic with early returns for each
				// precondition check (object is identifier, not 'this', found in bindings,
				// member exists), returning nullopt to fall through to other handlers.
					auto dot_assign = [&]() -> std::optional<EvalResult> {
						const ASTNode& obj = ma_ptr->object();
						if (!obj.is<ExpressionNode>())
							return std::nullopt;
						const auto* obj_id = std::get_if<IdentifierNode>(&obj.as<ExpressionNode>());
						if (!obj_id || obj_id->name() == "this")
							return std::nullopt;
						EvalResult* obj_binding = findMutableBindingValue(obj_id->name(), bindings, context);
						if (!obj_binding)
							return std::nullopt;
						auto member_it = obj_binding->object_member_bindings.find(ma_ptr->member_name());
						if (member_it == obj_binding->object_member_bindings.end())
							return std::nullopt;
						auto rhs_result = evaluate_expression_with_bindings(bin_op.get_rhs(), bindings, context);
						if (!rhs_result.success())
							return rhs_result;
						auto result = apply_op_to(member_it->second, rhs_result);
						if (result.success()) {
							// Recalculate the container's is_indeterminate flag after the member write
							// so that whole-struct reads (e.g., `return s;`) are not incorrectly rejected.
							if (obj_binding->is_indeterminate) {
								obj_binding->is_indeterminate = false;
								for (const auto& [_, mv] : obj_binding->object_member_bindings) {
									if (mv.is_indeterminate) {
										obj_binding->is_indeterminate = true;
										break;
									}
								}
							}
							refreshPointerSnapshotsForBinding(obj_id->name(), *obj_binding, bindings, context);
						}
						return result;
					};
					if (auto result = dot_assign())
						return *result;
				}

			// Handle subscript assignment: arr[i] = value  (C++20 constexpr heap arrays)
				// LHS is ArraySubscriptNode(array=expr_that_yields_pointer, index=expr)
				if (std::holds_alternative<ArraySubscriptNode>(lhs_expr)) {
					const auto& subscript = std::get<ArraySubscriptNode>(lhs_expr);
					auto arr_result = evaluate_expression_with_bindings(subscript.array_expr(), bindings, context);
					if (!arr_result.success())
						return arr_result;
					auto idx_result = evaluate_expression_with_bindings(subscript.index_expr(), bindings, context);
					if (!idx_result.success())
						return idx_result;
					int64_t idx = idx_result.as_int();

					auto rhs_result = evaluate_expression_with_bindings(bin_op.get_rhs(), bindings, context);
					if (!rhs_result.success())
						return rhs_result;

					// Case 1: arr_result is a pointer into the constexpr heap (new int[n])
					if (arr_result.pointer_to_var.isValid()) {
						StringHandle heap_key = arr_result.pointer_to_var;
						int64_t base_offset = arr_result.pointer_offset;
						auto heap_it = context.constexpr_heap.find(heap_key);
						if (heap_it != context.constexpr_heap.end() && !heap_it->second.freed) {
							EvalResult& heap_val = heap_it->second.value;
							if (heap_val.is_array) {
								int64_t final_idx = base_offset + idx;
								if (final_idx < 0 || static_cast<size_t>(final_idx) >= heap_val.array_elements.size()) {
									return EvalResult::error("Array index out of bounds in constexpr subscript assignment");
								}
								return apply_op_to(heap_val.array_elements[static_cast<size_t>(final_idx)], rhs_result);
							}
						}
					}

					// Case 2: arr_result is a local array binding
					if (arr_result.is_array && arr_result.array_origin_var.isValid()) {
						std::string_view arr_name = StringTable::getStringView(arr_result.array_origin_var);
						EvalResult* bound = findMutableBindingValue(arr_name, bindings, context);
						if (bound && bound->is_array) {
							if (idx < 0 || static_cast<size_t>(idx) >= bound->array_elements.size()) {
								return EvalResult::error("Array index out of bounds in constexpr subscript assignment");
							}
							auto result = apply_op_to(bound->array_elements[static_cast<size_t>(idx)], rhs_result);
							if (result.success()) {
								refreshPointerSnapshotsForBinding(arr_name, *bound, bindings, context);
							}
							return result;
						}
					}
					// Case 3: nested subscript assignment for multi-dimensional local arrays.
					// Pattern: arr[i][j]...[k] = value.
					{
						std::vector<int64_t> nested_indices;
						nested_indices.push_back(idx);

						ASTNode base_expr = subscript.array_expr();
						while (const ArraySubscriptNode* parent_subscript = tryGetNode<ArraySubscriptNode>(base_expr)) {
							auto parent_idx_result = evaluate_expression_with_bindings(parent_subscript->index_expr(), bindings, context);
							if (!parent_idx_result.success())
								return parent_idx_result;
							nested_indices.push_back(parent_idx_result.as_int());
							base_expr = parent_subscript->array_expr();
						}

						if (nested_indices.size() > 1) {
							std::string_view base_name = getIdentifierNameFromAstNode(base_expr);
							if (!base_name.empty()) {
								EvalResult* base_bound = findMutableBindingValue(base_name, bindings, context);
								if (base_bound && base_bound->is_array) {
									EvalResult* current = base_bound;
									for (auto it = nested_indices.rbegin(); it != nested_indices.rend(); ++it) {
										int64_t current_idx = *it;
										if (!current->is_array) {
											return EvalResult::error("Subscript assignment target is not an array in constexpr multi-dimensional subscript assignment");
										}
										if (current_idx < 0 || static_cast<size_t>(current_idx) >= current->array_elements.size()) {
											return EvalResult::error("Array index out of bounds in constexpr multi-dimensional subscript assignment");
										}
										if (it + 1 == nested_indices.rend()) {
											auto result = apply_op_to(current->array_elements[static_cast<size_t>(current_idx)], rhs_result);
											if (result.success()) {
												refreshPointerSnapshotsForBinding(base_name, *base_bound, bindings, context);
											}
											return result;
										}
										current = &current->array_elements[static_cast<size_t>(current_idx)];
									}
								}
							}
						}
					}
					return EvalResult::error("Subscript assignment target is not a constexpr heap array or local array");
				}
			}
			return EvalResult::error("Left-hand side of assignment must be a variable");
		}

		// Regular binary operators (non-assignment)
		auto lhs_result = evaluate_expression_with_bindings(bin_op.get_lhs(), bindings, context);
		if (!lhs_result.success())
			return lhs_result;

		if (op == "&&" || op == "||") {
			const bool lhs_has_overloaded_logical =
				bin_op.has_resolved_operator_overload() ||
				lhs_result.object_type_index.is_valid();
			if (!lhs_has_overloaded_logical) {
				const bool lhs_bool = lhs_result.pointer_to_var.isValid() ? true : lhs_result.as_bool();
				if (op == "&&" && !lhs_bool)
					return EvalResult::from_bool(false);
				if (op == "||" && lhs_bool)
					return EvalResult::from_bool(true);
			}
		}

		auto rhs_result = evaluate_expression_with_bindings(bin_op.get_rhs(), bindings, context);
		if (!rhs_result.success())
			return rhs_result;

		if (auto member_operator_result =
				try_evaluate_constexpr_member_binary_operator(bin_op, lhs_result, rhs_result, context)) {
			return *member_operator_result;
		}
		if (op == "&&" || op == "||") {
			const bool lhs_bool = lhs_result.pointer_to_var.isValid() ? true : lhs_result.as_bool();
			const bool rhs_bool = rhs_result.pointer_to_var.isValid() ? true : rhs_result.as_bool();
			return EvalResult::from_bool(op == "&&" ? (lhs_bool && rhs_bool) : (lhs_bool || rhs_bool));
		}

		return apply_binary_op(lhs_result, rhs_result, bin_op.op(), &context, &bindings);
	}

	// Handle unary operators (including ++ and --)
	if (std::holds_alternative<UnaryOperatorNode>(expr)) {
		const auto& unary_op = std::get<UnaryOperatorNode>(expr);
		std::string_view op = unary_op.op();

		// Handle increment and decrement operators (they modify bindings)
		if (op == "++" || op == "--") {
			const ASTNode& operand = unary_op.get_operand();
			std::optional<EvalResult> resolve_error;
			if (std::optional<BoundWriteTarget> target = resolveBoundWriteTarget(
					operand, bindings, context, evaluate_expression_with_bindings_const, resolve_error);
				target.has_value() && target->slot != nullptr) {
				EvalResult current = *target->slot;
				if (current.is_indeterminate) {
					return EvalResult::error(
						"Read of indeterminate value in constant expression "
						"(uninitialized object)");
				}

				EvalResult one = EvalResult::from_int(1);
				EvalResult new_value = (op == "++")
					? apply_binary_op(current, one, "+", &context, &bindings)
					: apply_binary_op(current, one, "-", &context, &bindings);
				if (!new_value.success())
					return new_value;

				if (current.exact_type.has_value() &&
					is_unsigned_integer_type(current.exact_type->category())) {
					new_value = EvalResult::from_uint(apply_uint_type_mask(new_value.as_uint_raw(), current.exact_type));
					new_value.set_exact_type(*current.exact_type);
				}

				*target->slot = new_value;
				if (!target->root_name.empty()) {
					if (EvalResult* root_binding = findMutableBindingValue(target->root_name, bindings, context)) {
						refreshPointerSnapshotsForBinding(target->root_name, *root_binding, bindings, context);
					}
				}

				return unary_op.is_prefix() ? new_value : current;
			}
			if (resolve_error.has_value())
				return *resolve_error;
			return EvalResult::error("Operand of increment/decrement must be a modifiable constexpr lvalue");
		}

		// Handle address-of (&): return a pointer-to-variable result without evaluating the operand.
		if (op == "&") {
			const ASTNode& operand = unary_op.get_operand();
			if (operand.is<ExpressionNode>()) {
				const ExpressionNode& operand_expr = operand.as<ExpressionNode>();
				// &identifier  or  &this->member
				std::string_view simple_name;
				if (const auto* id = std::get_if<IdentifierNode>(&operand_expr)) {
					simple_name = id->name();
				} else if (const auto* ma = std::get_if<MemberAccessNode>(&operand_expr)) {
					if (const IdentifierNode* obj_id = tryGetIdentifier(ma->object())) {
						if (obj_id->name() == "this")
							simple_name = ma->member_name();
					}
				}
				if (!simple_name.empty()) {
					EvalResult ptr_result = EvalResult::from_pointer(simple_name);
					// Snapshot the current value so the pointer can be dereferenced in a
					// different scope (e.g., when passed as an argument to another function).
					const EvalResult* snap = findBindingValue(simple_name, bindings, context);
					if (snap)
						ptr_result.pointer_value_snapshot = {*snap};
					return ptr_result;
				}
				// &arr[i]: address of array element → pointer with offset.
				// Recognises both plain identifiers (&arr[i]) and this->member form (&this->arr[i]).
				if (const auto* subscript = std::get_if<ArraySubscriptNode>(&operand_expr)) {
					std::string_view arr_name = getArrayNameForAddressOf(subscript->array_expr());
					if (!arr_name.empty()) {
						auto idx_result = evaluate_expression_with_bindings(subscript->index_expr(), bindings, context);
						if (!idx_result.success())
							return idx_result;
						int64_t elem_offset = idx_result.as_int();
						EvalResult ptr_result = EvalResult::from_pointer(arr_name, elem_offset);
						// Snapshot the full array so the pointer can be dereferenced at any
						// valid offset in a different scope (e.g., p[0]+p[1]+p[2] after
						// cross-scope call).  This mirrors refreshPointerSnapshotsForBindingInMap.
						const EvalResult* arr_eval = findBindingValue(arr_name, bindings, context);
						if (arr_eval && arr_eval->is_array && elem_offset >= 0) {
							if (!arr_eval->array_elements.empty()) {
								ptr_result.pointer_value_snapshot = arr_eval->array_elements;
							} else if (!arr_eval->array_values.empty()) {
								ptr_result.pointer_value_snapshot.reserve(arr_eval->array_values.size());
								for (int64_t v : arr_eval->array_values) {
									ptr_result.pointer_value_snapshot.push_back(EvalResult::from_int(v));
								}
							}
						}
						return ptr_result;
					}
				}
			}
			return EvalResult::error("Address-of operator (&) is only supported on named variables and array elements in constant expressions");
		}

		// Regular unary operators
		auto operand_result = evaluate_expression_with_bindings(unary_op.get_operand(), bindings, context);
		if (!operand_result.success())
			return operand_result;

		// Handle dereference (*): look up the pointed-to variable.
		if (op == "*") {
			if (operand_result.pointer_to_var.isValid()) {
				return deref_pointer_with_bindings(operand_result, bindings, context);
			}
			return EvalResult::error(
				"Dereference operator (*) on a non-pointer value in constant expressions",
				EvalErrorType::NotConstantExpression);
		}

		return apply_unary_op(operand_result, op);
	}

	// For ternary operators
	if (std::holds_alternative<TernaryOperatorNode>(expr)) {
		const auto& ternary = std::get<TernaryOperatorNode>(expr);
		auto cond_result = evaluate_expression_with_bindings(ternary.condition(), bindings, context);

		if (!cond_result.success())
			return cond_result;

		if (cond_result.pointer_to_var.isValid() ? true : cond_result.as_bool()) {
			return evaluate_expression_with_bindings(ternary.true_expr(), bindings, context);
		} else {
			return evaluate_expression_with_bindings(ternary.false_expr(), bindings, context);
		}
	}

	if (const auto* call_expr = std::get_if<CallExprNode>(&expr)) {
		if (call_expr->has_receiver()) {
			if (auto call_result = try_evaluate_bound_member_operator_call(*call_expr, bindings, context, &bindings)) {
				return *call_result;
			}
			if (auto member_call_result = try_evaluate_bound_member_function_call(*call_expr, bindings, context, &bindings)) {
				return *member_call_result;
			}
			return evaluate_member_function_call(*call_expr, context);
		}

		if (const FunctionDeclarationNode* function_decl = call_expr->callee().function_declaration_or_null();
			function_decl && !function_decl->is_static() && context.struct_info) {
			Token this_token(
				Token::Type::Identifier,
				"this"sv,
				call_expr->called_from().line(),
				call_expr->called_from().column(),
				call_expr->called_from().file_index());
			ExpressionNode this_expr = IdentifierNode(this_token);
			CallExprNode member_call = makeResolvedMemberCallExpr(
				ASTNode(&this_expr),
				*function_decl,
				copyCallArguments(call_expr->arguments()),
				call_expr->called_from());
			ExpressionNode member_expr = member_call;
			if (auto call_result = try_evaluate_bound_member_operator_call(member_expr, bindings, context, &bindings)) {
				return *call_result;
			}
			if (auto member_call_result = try_evaluate_bound_member_function_call(member_expr, bindings, context, &bindings)) {
				return *member_call_result;
			}
			return evaluate_member_function_call(member_call, context);
		}

		return evaluate_function_call_with_outer_bindings(*call_expr, bindings, context, &bindings);
	}

	// For direct lambda operator() calls inside a bound constexpr context
	if (auto call_result = try_evaluate_bound_member_operator_call(expr, bindings, context, &bindings)) {
		return *call_result;
	}

	if (auto member_call_result = try_evaluate_bound_member_function_call(expr, bindings, context, &bindings)) {
		return *member_call_result;
	}

	// For member access on 'this' (e.g., this->x in a member function)
	// Reading is handled by the fall-through to evaluate_expression_with_bindings_const below.
	// Writing (this->x = ...) is handled above in the assignment operator branch.

	// For new-expressions (C++20 constexpr dynamic allocation)
	if (const auto* new_expr = std::get_if<NewExpressionNode>(&expr)) {
		return evaluate_new_expression(*new_expr, context, &bindings, &bindings);
	}

	// For delete-expressions (C++20 constexpr dynamic deallocation)
	if (const auto* del_expr = std::get_if<DeleteExpressionNode>(&expr)) {
		return evaluate_delete_expression(*del_expr, context, &bindings, &bindings);
	}

	// Handle InitializerListConstructionNode with mutable bindings so the synthetic
	// backing array is stored in the local binding map and pointer dereferences via
	// name lookup succeed inside the called function body.
	if (const auto* ilist_node = std::get_if<InitializerListConstructionNode>(&expr)) {
		return evaluate_initializer_list_construction(*ilist_node, bindings, context, &bindings);
	}

	// For other expression types, use the const version (cast bindings to const)
	const std::unordered_map<std::string_view, EvalResult>& const_bindings = bindings;
	return evaluate_expression_with_bindings_const(expr_node, const_bindings, context);
}

// Original const version for backward compatibility
EvalResult Evaluator::evaluate_expression_with_bindings_const(
	const ASTNode& expr_node,
	const std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context) {
	return evaluate_expression_with_bindings_dispatch(expr_node, bindings, context, evaluate_expression_with_bindings_const, nullptr);
}

EvalResult Evaluator::evaluate_expression_with_bindings_dispatch(
	const ASTNode& expr_node,
	const std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context,
	RecursiveBindEvalFn recursive_eval,
	std::unordered_map<std::string_view, EvalResult>* mutable_bindings) {

	if (!expr_node.is<ExpressionNode>()) {
		return EvalResult::error("Not an expression node");
	}

	const ExpressionNode& expr = expr_node.as<ExpressionNode>();

	if (const auto* lambda_expression = std::get_if<LambdaExpressionNode>(&expr)) {
		return materialize_lambda_value(*lambda_expression, context, &bindings);
	}

	// Check if it's an identifier that matches a parameter
	if (std::holds_alternative<IdentifierNode>(expr)) {
		const IdentifierNode& id = std::get<IdentifierNode>(expr);

		// Fast path: pre-resolved Local bindings are always in the bindings map
		if (id.binding() == IdentifierBinding::Local) {
			if (const EvalResult* bound_value = findBindingValue(id.name(), bindings, context)) {
				return read_bound_identifier_value(*bound_value, id.name(), bindings, context);
			}
			// fall through to existing logic as safety net
		}

		std::string_view name = id.name();

		// Check if it's a bound parameter
		if (const EvalResult* bound_value = findBindingValue(name, bindings, context)) {
			return read_bound_identifier_value(*bound_value, name, bindings, context);
		}

		// Not a parameter, evaluate normally
		return evaluate_identifier(id, context);
	}

	if (auto member_result = try_evaluate_bound_member_access(expr, bindings, context)) {
		return *member_result;
	}

	if (auto array_result = try_evaluate_bound_array_subscript(expr, bindings, context)) {
		return *array_result;
	}

	if (std::holds_alternative<BinaryOperatorNode>(expr)) {
		const auto& bin_op = std::get<BinaryOperatorNode>(expr);
		std::string_view op = bin_op.op();

		auto lhs_result = recursive_eval(bin_op.get_lhs(), bindings, context);
		if (!lhs_result.success())
			return lhs_result;

		if (op == "&&" || op == "||") {
			const bool lhs_has_overloaded_logical =
				bin_op.has_resolved_operator_overload() ||
				lhs_result.object_type_index.is_valid();
			if (!lhs_has_overloaded_logical) {
				const bool lhs_bool = lhs_result.pointer_to_var.isValid() ? true : lhs_result.as_bool();
				if (op == "&&" && !lhs_bool)
					return EvalResult::from_bool(false);
				if (op == "||" && lhs_bool)
					return EvalResult::from_bool(true);
			}
		}

		auto rhs_result = recursive_eval(bin_op.get_rhs(), bindings, context);
		if (!rhs_result.success())
			return rhs_result;

		if (auto member_operator_result =
				try_evaluate_constexpr_member_binary_operator(bin_op, lhs_result, rhs_result, context)) {
			return *member_operator_result;
		}
		if (op == "&&" || op == "||") {
			const bool lhs_bool = lhs_result.pointer_to_var.isValid() ? true : lhs_result.as_bool();
			const bool rhs_bool = rhs_result.pointer_to_var.isValid() ? true : rhs_result.as_bool();
			return EvalResult::from_bool(op == "&&" ? (lhs_bool && rhs_bool) : (lhs_bool || rhs_bool));
		}

		return apply_binary_op(lhs_result, rhs_result, bin_op.op(), &context, &bindings);
	}

	// For unary operators
	if (const auto* unary_op = std::get_if<UnaryOperatorNode>(&expr)) {
		std::string_view op = unary_op->op();

		// Handle address-of (&): return a pointer-to-variable result without evaluating the operand.
		if (op == "&") {
			const ASTNode& operand = unary_op->get_operand();
			if (operand.is<ExpressionNode>()) {
				const ExpressionNode& operand_expr = operand.as<ExpressionNode>();
				if (const auto* qualified_id = std::get_if<QualifiedIdentifierNode>(&operand_expr)) {
					if (auto member = tryResolveMemberPointerTarget(*qualified_id)) {
						return EvalResult::from_member_pointer(member->member_name, member->offset);
					}
				}
				// &identifier  or  &this->member
				std::string_view simple_name;
				if (const auto* id = std::get_if<IdentifierNode>(&operand_expr)) {
					simple_name = id->name();
				} else if (const auto* ma = std::get_if<MemberAccessNode>(&operand_expr)) {
					if (const IdentifierNode* obj_id = tryGetIdentifier(ma->object())) {
						if (obj_id->name() == "this")
							simple_name = ma->member_name();
					}
				}
				if (!simple_name.empty()) {
					EvalResult ptr_result = EvalResult::from_pointer(simple_name);
					// Snapshot the current value so the pointer can be dereferenced in a
					// different scope (e.g., when passed as an argument to another function).
					const EvalResult* snap = findBindingValue(simple_name, bindings, context);
					if (snap)
						ptr_result.pointer_value_snapshot = {*snap};
					return ptr_result;
				}
				// &arr[i]: address of array element → pointer with offset.
				// Recognises both plain identifiers (&arr[i]) and this->member form (&this->arr[i]).
				if (const auto* subscript = std::get_if<ArraySubscriptNode>(&operand_expr)) {
					std::string_view arr_name = getArrayNameForAddressOf(subscript->array_expr());
					if (!arr_name.empty()) {
						auto idx_result = recursive_eval(subscript->index_expr(), bindings, context);
						if (!idx_result.success())
							return idx_result;
						int64_t elem_offset = idx_result.as_int();
						EvalResult ptr_result = EvalResult::from_pointer(arr_name, elem_offset);
						// Snapshot the full array so the pointer can be dereferenced at any
						// valid offset in a different scope (e.g., p[0]+p[1]+p[2] after
						// cross-scope call).  This mirrors refreshPointerSnapshotsForBindingInMap.
						const EvalResult* arr_eval = findBindingValue(arr_name, bindings, context);
						if (arr_eval && arr_eval->is_array && elem_offset >= 0) {
							if (!arr_eval->array_elements.empty()) {
								ptr_result.pointer_value_snapshot = arr_eval->array_elements;
							} else if (!arr_eval->array_values.empty()) {
								ptr_result.pointer_value_snapshot.reserve(arr_eval->array_values.size());
								for (int64_t v : arr_eval->array_values) {
									ptr_result.pointer_value_snapshot.push_back(EvalResult::from_int(v));
								}
							}
						}
						return ptr_result;
					}
				}
			}
			return EvalResult::error("Address-of operator (&) is only supported on named variables and array elements in constant expressions");
		}

	// Special case: *this in a constexpr member function body.
	// Constructs an EvalResult representing the current object state.
	// Uses context.struct_type_index (set alongside struct_info when entering a member function body)
	// to avoid an O(n) linear search through gTypeInfo.
		if (op == "*") {
			const ASTNode& operand = unary_op->get_operand();
			if (operand.is<ExpressionNode>()) {
				const ExpressionNode& operand_expr = operand.as<ExpressionNode>();
				if (const auto* id = std::get_if<IdentifierNode>(&operand_expr)) {
					if (id->name() == "this" && context.struct_info) {
						EvalResult this_obj = EvalResult::from_int(0);
					// Use the cached type index; validate it before trusting it.
						if (!tryGetTypeInfo(context.struct_type_index)) {
						// struct_type_index must be set alongside struct_info when entering a
						// member function body. This indicates a call site that sets struct_info
						// but forgets to populate struct_type_index.
							return EvalResult::error("Internal error: *this used in constexpr member function but struct_type_index is not set — ensure evaluate_member_function_call sets context.struct_type_index");
						}
						this_obj.object_type_index = context.struct_type_index;
					// Copy current member bindings into the object
						for (const auto& member : context.struct_info->members) {
							std::string_view member_name = StringTable::getStringView(member.getName());
							auto it = bindings.find(member_name);
							if (it != bindings.end()) {
								this_obj.object_member_bindings[member_name] = it->second;
							}
						}
						return this_obj;
					}
				}
			}
		}

		auto operand_result = recursive_eval(unary_op->get_operand(), bindings, context);
		if (!operand_result.success())
			return operand_result;

		// Handle dereference (*): look up the pointed-to variable.
		if (op == "*") {
			if (operand_result.pointer_to_var.isValid()) {
				return deref_pointer_with_bindings(operand_result, bindings, context);
			}
			return EvalResult::error(
				"Dereference operator (*) on a non-pointer value in constant expressions",
				EvalErrorType::NotConstantExpression);
		}

		return apply_unary_op(operand_result, op);
	}

	// For ternary operators
	if (std::holds_alternative<TernaryOperatorNode>(expr)) {
		const auto& ternary = std::get<TernaryOperatorNode>(expr);
		auto cond_result = recursive_eval(ternary.condition(), bindings, context);

		if (!cond_result.success())
			return cond_result;

		if (cond_result.pointer_to_var.isValid() ? true : cond_result.as_bool()) {
			return recursive_eval(ternary.true_expr(), bindings, context);
		} else {
			return recursive_eval(ternary.false_expr(), bindings, context);
		}
	}

	// For function calls (for recursion)
	if (const auto* call_expr = std::get_if<CallExprNode>(&expr)) {
		if (call_expr->has_receiver()) {
			if (auto call_result = try_evaluate_bound_member_operator_call(*call_expr, bindings, context, mutable_bindings)) {
				return *call_result;
			}
			if (auto member_call_result = try_evaluate_bound_member_function_call(*call_expr, bindings, context, mutable_bindings)) {
				return *member_call_result;
			}
			return evaluate_member_function_call(*call_expr, context);
		}

		if (const FunctionDeclarationNode* function_decl = call_expr->callee().function_declaration_or_null();
			function_decl && !function_decl->is_static() && context.struct_info) {
			Token this_token(
				Token::Type::Identifier,
				"this"sv,
				call_expr->called_from().line(),
				call_expr->called_from().column(),
				call_expr->called_from().file_index());
			ExpressionNode this_expr = IdentifierNode(this_token);
			CallExprNode member_call = makeResolvedMemberCallExpr(
				ASTNode(&this_expr),
				*function_decl,
				copyCallArguments(call_expr->arguments()),
				call_expr->called_from());
			ExpressionNode member_expr = member_call;
			if (auto call_result = try_evaluate_bound_member_operator_call(member_expr, bindings, context, mutable_bindings)) {
				return *call_result;
			}
			if (auto member_call_result = try_evaluate_bound_member_function_call(member_expr, bindings, context, mutable_bindings)) {
				return *member_call_result;
			}
			return evaluate_member_function_call(member_call, context);
		}

		return evaluate_function_call_with_outer_bindings(*call_expr, bindings, context, mutable_bindings);
	}

	// For direct lambda operator() calls inside a bound constexpr context
	if (auto call_result = try_evaluate_bound_member_operator_call(expr, bindings, context, mutable_bindings)) {
		return *call_result;
	}

	if (auto member_call_result = try_evaluate_bound_member_function_call(expr, bindings, context, mutable_bindings)) {
		return *member_call_result;
	}

	if (const auto* new_expr = std::get_if<NewExpressionNode>(&expr)) {
		return evaluate_new_expression(*new_expr, context, &bindings, mutable_bindings);
	}

	if (const auto* del_expr = std::get_if<DeleteExpressionNode>(&expr)) {
		return evaluate_delete_expression(*del_expr, context, &bindings, mutable_bindings);
	}

	// For member access on 'this' (e.g., this->x in a member function)
	// This handles implicit member accesses like 'x' which parser transforms to 'this->x'
	if (std::holds_alternative<MemberAccessNode>(expr)) {
		const auto& member_access = std::get<MemberAccessNode>(expr);
		std::string_view member_name = member_access.member_name();

		// Check if the object is 'this' (implicit member access)
		const ASTNode& obj = member_access.object();
		if (obj.is<ExpressionNode>()) {
			const ExpressionNode& obj_expr = obj.as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(obj_expr)) {
				const IdentifierNode& obj_id = std::get<IdentifierNode>(obj_expr);
				if (obj_id.name() == "this") {
					// This is an implicit member access - look up in bindings
					auto it = bindings.find(member_name);
					if (it != bindings.end()) {
						return it->second;  // Return the bound member value
					}
					if (const EvalResult* this_binding = findBindingValue("this", bindings, context)) {
						auto this_member_it = this_binding->object_member_bindings.find(member_name);
						if (this_member_it != this_binding->object_member_bindings.end()) {
							return this_member_it->second;
						}
					}
					return EvalResult::error("Member not found in constexpr object: " + std::string(member_name));
				}
				// Handle arrow access (ptr->member) where ptr is a pointer in local bindings.
				if (member_access.is_arrow()) {
					auto binding_it = bindings.find(obj_id.name());
					if (binding_it != bindings.end() && binding_it->second.pointer_to_var.isValid()) {
						const EvalResult& ptr_eval = binding_it->second;
						std::string_view ptr_var_name = StringTable::getStringView(ptr_eval.pointer_to_var);
						// First try direct pointer dereference against active bindings.
						// This handles struct-array pointers (any offset) and synthesized
						// backing arrays such as std::initializer_list storage.
						auto elem_result = deref_pointer_with_bindings(ptr_eval, bindings, context);
						if (!elem_result.success()) {
							if (!isRecoverablePointerDerefFailure(elem_result)) {
								return elem_result;
							}
						} else {
							auto member_it = elem_result.object_member_bindings.find(member_name);
							if (member_it != elem_result.object_member_bindings.end()) {
								return member_it->second;
							}
						}
						if (ptr_eval.pointer_offset != 0) {
							return EvalResult::error("Arrow member access (->): member '" + std::string(member_name) +
													 "' not found at offset " + std::to_string(ptr_eval.pointer_offset));
						}
						// Check snapshot first (covers pointers to local / outer-scope variables).
						if (!ptr_eval.pointer_value_snapshot.empty()) {
							const EvalResult& snapshot = ptr_eval.pointer_value_snapshot[0];
							auto member_it = snapshot.object_member_bindings.find(member_name);
							if (member_it != snapshot.object_member_bindings.end()) {
								return member_it->second;
							}
						}
						return evaluate_arrow_member_from_pointer_var(
							ptr_var_name, member_name, context, /*check_static=*/true);
					}
				}
			}
		}
		// Fall through to normal evaluation for non-this member access
	}

	if (auto array_result = try_evaluate_bound_array_subscript(expr, bindings, context)) {
		return *array_result;
	}

	// Handle StaticCastNode (static_cast<T>(e) and C-style casts) using the
	// bindings-aware recursive evaluator so that local variables and function
	// parameters are visible inside the cast expression.
	if (const auto* static_cast_node = std::get_if<StaticCastNode>(&expr)) {
		const TypeSpecifierNode& type_spec = static_cast_node->target_type();
		// Evaluate the inner expression with bindings.
		auto inner_result = recursive_eval(static_cast_node->expr(), bindings, context);
		if (!inner_result.success()) {
			return inner_result;
		}
		// For struct/user-defined/enum types, a static_cast that only changes cv/ref
		// qualification (e.g., static_cast<const T&>(obj)) should pass through the
		// value unchanged — no scalar conversion is needed.  This mirrors the
		// typesMatchIgnoringCvAndRef short-circuit in evaluate_static_cast.
		// Guard with a type-match check so that cross-struct casts fall through to
		// the scalar conversion switch (which correctly errors for struct types).
		if (needs_type_index(type_spec.type())) {
			auto source_type = tryGetExpressionType(inner_result, static_cast_node->expr(), context);
			if (source_type.has_value() && typesMatchIgnoringCvAndRef(type_spec, *source_type)) {
				inner_result.set_exact_type(type_spec);
				return inner_result;
			}
			// Source type unavailable or doesn't match — fall through to the
			// scalar conversion switch which will error for struct/enum types.
		}
		// Apply the target-type conversion using the shared helper (mirrors evaluate_expr_node).
		return convertEvalResultToTargetType(type_spec, inner_result, "Unsupported type in cast for constant evaluation");
	}

	// Handle ConstCastNode (const_cast<T>(e)) using the bindings-aware recursive
	// evaluator so that local variables and function parameters are visible inside
	// the cast expression.  const_cast only changes cv/ref qualification — no type
	// conversion is performed; the value/object identity is preserved as-is.
	if (const auto* const_cast_node = std::get_if<ConstCastNode>(&expr)) {
		const TypeSpecifierNode& type_spec = const_cast_node->target_type();
		// Evaluate the inner expression with bindings.
		auto inner_result = recursive_eval(const_cast_node->expr(), bindings, context);
		if (!inner_result.success()) {
			return inner_result;
		}
		// Validate that const_cast only changes cv/ref qualification — reject
		// type-changing casts like const_cast<float*>(&int_value).  Uses the
		// shared Evaluator::typesMatchIgnoringCvAndRef / tryGetExpressionType.
		if (auto source_type = tryGetExpressionType(inner_result, const_cast_node->expr(), context);
			source_type.has_value() &&
			!typesMatchIgnoringCvAndRef(type_spec, *source_type)) {
			return EvalResult::error(
				"const_cast in constant expression may only change cv-qualification",
				EvalErrorType::NotConstantExpression);
		}
		// Only update the type metadata — no value conversion needed.
		inner_result.set_exact_type(type_spec);
		return inner_result;
	}

	// Handle ConstructorCallNode for struct types (e.g., Pair{a, b} inside constexpr function bodies).
	// When evaluating inside a function body with local bindings, we need outer_bindings to evaluate
	// constructor arguments that reference local variables.
	if (const auto* ctor_call = std::get_if<ConstructorCallNode>(&expr)) {
		const TypeSpecifierNode& type_spec = ctor_call->type_node();
		if (is_struct_type(type_spec.category()) && tryGetTypeInfo(type_spec.type_index())) {
			return materialize_constructor_object_value(*ctor_call, context, &bindings);
		}
	}

	if (const auto* member_pointer_access = std::get_if<PointerToMemberAccessNode>(&expr)) {
		auto member_pointer_result = recursive_eval(member_pointer_access->member_pointer(), bindings, context);
		if (!member_pointer_result.success()) {
			return member_pointer_result;
		}
		if (member_pointer_result.is_null_member_pointer) {
			return EvalResult::error(
				"Pointer-to-member access does not support null member pointers in constant expressions",
				EvalErrorType::NotConstantExpression);
		}
		if (!member_pointer_result.member_pointer_member.isValid()) {
			return EvalResult::error("Pointer-to-member access requires a compile-time constant data-member pointer");
		}

		Token member_token(
			Token::Type::Identifier,
			StringTable::getStringView(member_pointer_result.member_pointer_member),
			kSyntheticTokenLine,
			kSyntheticTokenColumn,
			kSyntheticTokenFileIndex);
		ExpressionNode synthetic_member_access =
			MemberAccessNode(member_pointer_access->object(), member_token, member_pointer_access->is_arrow());
		if (auto member_result = try_evaluate_bound_member_access(synthetic_member_access, bindings, context)) {
			return *member_result;
		}
		return evaluate_member_access(std::get<MemberAccessNode>(synthetic_member_access), context);
	}

	// Handle InitializerListConstructionNode (std::initializer_list<T>{e1, e2, ...}).
	// Creates a synthetic backing array, constructs begin/end (or begin/size) pointers with
	// a full value snapshot, and materialises the initializer_list struct from the result.
	if (const auto* ilist_node = std::get_if<InitializerListConstructionNode>(&expr)) {
		return evaluate_initializer_list_construction(*ilist_node, bindings, context, mutable_bindings);
	}

	// For literals and other expressions without parameters, evaluate normally
	return evaluate(expr_node, context);
}

} // namespace ConstExpr
