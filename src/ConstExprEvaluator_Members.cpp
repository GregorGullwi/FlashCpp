#include "ConstExprEvaluator.h"
#include <limits>

namespace ConstExpr {
const ConstructorDeclarationNode* Evaluator::find_matching_constructor_by_parameter_count(
	const StructTypeInfo* struct_info,
	size_t parameter_count) {
	if (!struct_info) {
		return nullptr;
	}

	auto ctor_candidates = struct_info->getConstructorsByParameterCount(parameter_count, false);
	if (ctor_candidates.empty()) {
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

std::optional<ASTNode> Evaluator::lookup_function_symbol(
	const FunctionCallNode& func_call,
	std::string_view fallback_name,
	const SymbolTable& symbols) {
	if (func_call.has_mangled_name()) {
		auto mangled_symbol = symbols.lookup(func_call.mangled_name_handle());
		if (mangled_symbol.has_value()) {
			return mangled_symbol;
		}
	}

	if (func_call.has_qualified_name()) {
		QualifiedIdentifier qi = QualifiedIdentifier::fromQualifiedName(
			func_call.qualified_name_handle(),
			symbols.get_current_namespace_handle());
		auto qualified_symbol = symbols.lookup_qualified(qi);
		if (qualified_symbol.has_value()) {
			return qualified_symbol;
		}
	}

	return symbols.lookup(fallback_name);
}

EvalResult Evaluator::evaluate_function_call_with_outer_bindings(
	const FunctionCallNode& func_call,
	const std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context,
	std::unordered_map<std::string_view, EvalResult>* mutable_bindings) {
	const DeclarationNode& func_decl_node = func_call.function_declaration();
	std::string_view func_name = func_decl_node.identifier_token().value();

	if (!context.symbols) {
		return EvalResult::error("Cannot evaluate function call: no symbol table provided");
	}

	auto bound_callable = bindings.find(func_name);
	if (bound_callable != bindings.end() && bound_callable->second.callable_var_decl) {
		return evaluate_callable_object(*bound_callable->second.callable_var_decl, func_call.arguments(), context, &bindings, mutable_bindings);
	}

	auto symbol_opt = lookup_function_symbol(func_call, func_name, *context.symbols);
	if (!symbol_opt.has_value()) {
		if (func_call.has_template_arguments() && context.parser) {
			auto var_result = tryEvaluateAsVariableTemplate(func_name, func_call, context);
			if (var_result.success()) {
				return var_result;
			}
		}
		return EvalResult::error("Undefined function in constant expression: " + std::string(func_name));
	}

	const ASTNode& symbol_node = symbol_opt.value();
	if (!symbol_node.is<FunctionDeclarationNode>()) {
		if (symbol_node.is<VariableDeclarationNode>()) {
			const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
			return evaluate_callable_object(var_decl, func_call.arguments(), context, &bindings, mutable_bindings);
		}

		if (symbol_node.is<TemplateVariableDeclarationNode>()) {
			auto var_result = tryEvaluateAsVariableTemplate(func_name, func_call, context);
			if (var_result.success()) {
				return var_result;
			}
		}
		return EvalResult::error("Identifier is not a function: " + std::string(func_name));
	}

	const FunctionDeclarationNode& func_decl = symbol_node.as<FunctionDeclarationNode>();
	if (!func_decl.is_constexpr()) {
		return EvalResult::error("Function in constant expression must be constexpr: " + std::string(func_name));
	}

	return evaluate_function_call_with_bindings(func_decl, func_call.arguments(), bindings, context);
}

std::optional<EvalResult> Evaluator::try_evaluate_bound_member_operator_call(
	const ExpressionNode& expr,
	const std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context,
	std::unordered_map<std::string_view, EvalResult>* mutable_bindings) {
	if (!std::holds_alternative<MemberFunctionCallNode>(expr)) {
		return std::nullopt;
	}

	const auto& member_func_call = std::get<MemberFunctionCallNode>(expr);
	std::string_view func_name = member_func_call.function_declaration().decl_node().identifier_token().value();
	if (func_name != "operator()") {
		return std::nullopt;
	}

	auto extract_callable_identifier = [&]() -> const IdentifierNode* {
		const ASTNode& object_expr = member_func_call.object();
		if (object_expr.is<IdentifierNode>()) {
			return &object_expr.as<IdentifierNode>();
		}
		if (object_expr.is<ExpressionNode>()) {
			const ExpressionNode& expr_node = object_expr.as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(expr_node)) {
				return &std::get<IdentifierNode>(expr_node);
			}
		}
		return nullptr;
	};

	auto extract_lambda_from_object_expr = [&]() -> const LambdaExpressionNode* {
		const ASTNode& object_expr = member_func_call.object();
		if (object_expr.is<LambdaExpressionNode>()) {
			return &object_expr.as<LambdaExpressionNode>();
		}
		if (object_expr.is<ExpressionNode>()) {
			const ExpressionNode& expr_node = object_expr.as<ExpressionNode>();
			if (std::holds_alternative<LambdaExpressionNode>(expr_node)) {
				return &std::get<LambdaExpressionNode>(expr_node);
			}
		}
		return nullptr;
	};

	if (const LambdaExpressionNode* lambda = extract_lambda_from_object_expr()) {
		return evaluate_lambda_call(*lambda, member_func_call.arguments(), context, &bindings, mutable_bindings);
	}

	if (const IdentifierNode* callable_id = extract_callable_identifier()) {
		auto callable_it = bindings.find(callable_id->name());
		if (callable_it != bindings.end() && callable_it->second.callable_var_decl) {
			return evaluate_callable_object(*callable_it->second.callable_var_decl, member_func_call.arguments(), context, &bindings, mutable_bindings);
		}
	}

	return std::nullopt;
}

std::optional<EvalResult> Evaluator::try_evaluate_bound_member_function_call(
	const ExpressionNode& expr,
	const std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context,
	std::unordered_map<std::string_view, EvalResult>* mutable_bindings) {
	if (!std::holds_alternative<MemberFunctionCallNode>(expr)) {
		return std::nullopt;
	}

	const auto& member_func_call = std::get<MemberFunctionCallNode>(expr);
	std::string_view func_name = member_func_call.function_declaration().decl_node().identifier_token().value();
	if (func_name == "operator()") {
		return std::nullopt;
	}
	if (!context.struct_info) {
		return std::nullopt;
	}

	auto extract_object_identifier = [&]() -> const IdentifierNode* {
		const ASTNode& object_expr = member_func_call.object();
		if (object_expr.is<IdentifierNode>()) {
			return &object_expr.as<IdentifierNode>();
		}
		if (object_expr.is<ExpressionNode>()) {
			const ExpressionNode& expr_node = object_expr.as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(expr_node)) {
				return &std::get<IdentifierNode>(expr_node);
			}
		}
		return nullptr;
	};

	const IdentifierNode* object_identifier = extract_object_identifier();
	if (!object_identifier || object_identifier->name() != "this") {
		return std::nullopt;
	}

	StringHandle func_name_handle = StringTable::getOrInternStringHandle(func_name);
	auto member_function_match = find_current_struct_member_function_candidate(
		func_name_handle,
		member_func_call.arguments().size(),
		context,
		MemberFunctionLookupMode::LookupOnly,
		false,
		true);
	if (member_function_match.ambiguous) {
		return EvalResult::error("Ambiguous member function overload in constant expression");
	}

	const FunctionDeclarationNode* actual_func = member_function_match.function;
	if (!actual_func) {
		return EvalResult::error("Member function not found: " + std::string(func_name));
	}
	if (!actual_func->is_constexpr()) {
		return EvalResult::error("Member function must be constexpr: " + std::string(func_name));
	}

	const auto& definition = actual_func->get_definition();
	if (!definition.has_value()) {
		return EvalResult::error("Constexpr member function has no body: " + std::string(func_name));
	}

	std::unordered_map<std::string_view, EvalResult> member_bindings;
	for (const auto& member : context.struct_info->members) {
		std::string_view member_name = StringTable::getStringView(member.getName());
		auto binding_it = bindings.find(member_name);
		if (binding_it != bindings.end()) {
			member_bindings[member_name] = binding_it->second;
		}
	}

	auto bind_result = bind_evaluated_arguments(
		actual_func->parameter_nodes(),
		member_func_call.arguments(),
		member_bindings,
		context,
		"Invalid parameter node in bound constexpr member function",
		&bindings);
	if (!bind_result.success()) {
		return bind_result;
	}

	auto saved_template_param_names = context.template_param_names;
	auto saved_template_args = context.template_args;
	auto restore_template_bindings = [&]() {
		context.template_param_names = std::move(saved_template_param_names);
		context.template_args = std::move(saved_template_args);
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
	} else {
		try_load_current_struct_template_bindings(context);
	}

	if (context.current_depth >= context.max_recursion_depth) {
		restore_template_bindings();
		return EvalResult::error("Constexpr recursion depth limit exceeded in bound member function call");
	}

	context.current_depth++;
	auto result = evaluate_block_with_bindings(
		definition.value(),
		member_bindings,
		context,
		"Member function body is not a block",
		"Constexpr member function did not return a value");
	context.current_depth--;
	if (result.success() && mutable_bindings) {
		for (const auto& member : context.struct_info->members) {
			std::string_view member_name = StringTable::getStringView(member.getName());
			auto member_it = member_bindings.find(member_name);
			if (member_it != member_bindings.end()) {
				(*mutable_bindings)[member_name] = member_it->second;
			}
		}
	}
	restore_template_bindings();
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
		if (func_decl.parameter_nodes().size() != argument_count) {
			continue;
		}

		if (result.function && detect_ambiguity) {
			result.ambiguous = true;
			return result;
		}

		result.function = &func_decl;
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
			member_func.getName() != function_name_handle ||
			!member_func.function_decl.is<FunctionDeclarationNode>()) {
			continue;
		}

		const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
		if (require_static && !func_decl.is_static()) {
			continue;
		}
		if (func_decl.parameter_nodes().size() != argument_count) {
			continue;
		}

		if (lookup_mode == MemberFunctionLookupMode::ConstexprEvaluable) {
			bool can_evaluate = func_decl.is_constexpr() ||
				(context.storage_duration == ConstExpr::StorageDuration::Static);
			if (!can_evaluate || !func_decl.get_definition().has_value()) {
				continue;
			}
		}

		if (result.function && detect_ambiguity) {
			result.ambiguous = true;
			return result;
		}

		result.function = &func_decl;
		if (!detect_ambiguity) {
			break;
		}
	}

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

	if (context.parser && LazyMemberInstantiationRegistry::getInstance().needsInstantiation(
			context.struct_info->name, function_name_handle)) {
		auto lazy_info_opt = LazyMemberInstantiationRegistry::getInstance().getLazyMemberInfo(
			context.struct_info->name, function_name_handle);
		if (lazy_info_opt.has_value()) {
			context.parser->instantiateLazyMemberFunction(*lazy_info_opt);
			LazyMemberInstantiationRegistry::getInstance().markInstantiated(
				context.struct_info->name, function_name_handle);
		}
	}

	result = find_member_function_candidate(
		context.struct_info,
		function_name_handle,
		argument_count,
		context,
		lookup_mode,
		require_static,
		detect_ambiguity_in_current_struct);
	if (result.function || result.ambiguous) {
		return result;
	}

	auto struct_type_it = gTypesByName.find(context.struct_info->name);
	if (struct_type_it != gTypesByName.end() && struct_type_it->second->isTemplateInstantiation()) {
		const TypeInfo* struct_type = struct_type_it->second;
		auto template_type_it = gTypesByName.find(struct_type->baseTemplateName());
		if (template_type_it != gTypesByName.end() && template_type_it->second->isStruct()) {
			return find_member_function_candidate(
				template_type_it->second->getStructInfo(),
				function_name_handle,
				argument_count,
				context,
				lookup_mode,
				require_static,
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
	return { static_member, owner_struct };
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
		return { &static_member_result.static_member->initializer, true };
	}

	auto member_name_handle = get_current_struct_static_lookup_name_handle(identifier, lookup_mode);
	if (!member_name_handle.has_value() || context.struct_node == nullptr) {
		return {};
	}

	for (const auto& static_member : context.struct_node->static_members()) {
		if (static_member.name == member_name_handle.value()) {
			return { &static_member.initializer, true };
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
	
	// Check if it's an identifier that matches a parameter
	if (std::holds_alternative<IdentifierNode>(expr)) {
		const IdentifierNode& id = std::get<IdentifierNode>(expr);

		// Fast path: pre-resolved Local bindings are always in the bindings map
		if (id.binding() == IdentifierBinding::Local) {
			auto it = bindings.find(id.name());
			if (it != bindings.end()) return it->second;
			// fall through to existing logic as safety net
		}

		std::string_view name = id.name();

		// Check if it's a bound parameter
		auto it = bindings.find(name);
		if (it != bindings.end()) {
			return it->second;  // Return the bound value
		}
		
		// Not a parameter, evaluate normally
		return evaluate_identifier(id, context);
	}
	
	// For binary operators, recursively evaluate with bindings
	if (std::holds_alternative<BinaryOperatorNode>(expr)) {
		const auto& bin_op = std::get<BinaryOperatorNode>(expr);
		std::string_view op = bin_op.op();
		
		// Handle assignment operators specially (they modify bindings)
		if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=") {
			// Get the left-hand side variable name
			const ASTNode& lhs = bin_op.get_lhs();
			if (lhs.is<ExpressionNode>()) {
				const ExpressionNode& lhs_expr = lhs.as<ExpressionNode>();

				// Determine the name of the variable being assigned to.
				// Two forms are accepted:
				//   1. Plain identifier:     x = ...
				//   2. this->member access:  this->x = ...
				std::string_view var_name;
				if (std::holds_alternative<IdentifierNode>(lhs_expr)) {
					var_name = std::get<IdentifierNode>(lhs_expr).name();
				} else if (std::holds_alternative<MemberAccessNode>(lhs_expr)) {
					const auto& ma = std::get<MemberAccessNode>(lhs_expr);
					const ASTNode& obj = ma.object();
					if (obj.is<ExpressionNode>()) {
						const ExpressionNode& obj_expr = obj.as<ExpressionNode>();
						if (std::holds_alternative<IdentifierNode>(obj_expr) &&
						    std::get<IdentifierNode>(obj_expr).name() == "this") {
							var_name = ma.member_name();
						}
					}
				}

				if (!var_name.empty()) {
					// Evaluate the right-hand side
					auto rhs_result = evaluate_expression_with_bindings(bin_op.get_rhs(), bindings, context);
					if (!rhs_result.success()) return rhs_result;
					
					// Perform the assignment
					if (op == "=") {
						bindings[var_name] = rhs_result;
						return rhs_result;
					} else {
						// Compound assignment - get current value first
						auto it = bindings.find(var_name);
						if (it == bindings.end()) {
							return EvalResult::error("Variable not found for compound assignment: " + std::string(var_name));
						}
						EvalResult current = it->second;
						
						// Apply the operation
						EvalResult new_value;
						if (op == "+=") {
							new_value = apply_binary_op(current, rhs_result, "+");
						} else if (op == "-=") {
							new_value = apply_binary_op(current, rhs_result, "-");
						} else if (op == "*=") {
							new_value = apply_binary_op(current, rhs_result, "*");
						} else if (op == "/=") {
							new_value = apply_binary_op(current, rhs_result, "/");
						} else if (op == "%=") {
							new_value = apply_binary_op(current, rhs_result, "%");
						}
						
						if (!new_value.success()) return new_value;
						bindings[var_name] = new_value;
						return new_value;
					}
				}
			}
			return EvalResult::error("Left-hand side of assignment must be a variable");
		}
		
		// Regular binary operators (non-assignment)
		auto lhs_result = evaluate_expression_with_bindings(bin_op.get_lhs(), bindings, context);
		auto rhs_result = evaluate_expression_with_bindings(bin_op.get_rhs(), bindings, context);
		
		if (!lhs_result.success()) return lhs_result;
		if (!rhs_result.success()) return rhs_result;
		
		return apply_binary_op(lhs_result, rhs_result, bin_op.op());
	}
	
	// Handle unary operators (including ++ and --)
	if (std::holds_alternative<UnaryOperatorNode>(expr)) {
		const auto& unary_op = std::get<UnaryOperatorNode>(expr);
		std::string_view op = unary_op.op();
		
		// Handle increment and decrement operators (they modify bindings)
		if (op == "++" || op == "--") {
			const ASTNode& operand = unary_op.get_operand();
			if (operand.is<ExpressionNode>()) {
				const ExpressionNode& operand_expr = operand.as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(operand_expr)) {
					const IdentifierNode& id = std::get<IdentifierNode>(operand_expr);
					std::string_view var_name = id.name();
					
					// Get current value
					auto it = bindings.find(var_name);
					if (it == bindings.end()) {
						return EvalResult::error("Variable not found for increment/decrement: " + std::string(var_name));
					}
					EvalResult current = it->second;
					
					// Calculate new value
					EvalResult one = EvalResult::from_int(1);
					EvalResult new_value;
					if (op == "++") {
						new_value = apply_binary_op(current, one, "+");
					} else {
						new_value = apply_binary_op(current, one, "-");
					}
					
					if (!new_value.success()) return new_value;
					bindings[var_name] = new_value;
					
					// Return old value for postfix, new value for prefix
					if (unary_op.is_prefix()) {
						return new_value;  // Prefix: return new value
					} else {
						return current;  // Postfix: return old value
					}
				}
			}
			return EvalResult::error("Operand of increment/decrement must be a variable");
		}
		
		// Regular unary operators
		auto operand_result = evaluate_expression_with_bindings(unary_op.get_operand(), bindings, context);
		if (!operand_result.success()) return operand_result;
		return apply_unary_op(operand_result, op);
	}
	
	// For ternary operators
	if (std::holds_alternative<TernaryOperatorNode>(expr)) {
		const auto& ternary = std::get<TernaryOperatorNode>(expr);
		auto cond_result = evaluate_expression_with_bindings(ternary.condition(), bindings, context);
		
		if (!cond_result.success()) return cond_result;
		
		if (cond_result.as_bool()) {
			return evaluate_expression_with_bindings(ternary.true_expr(), bindings, context);
		} else {
			return evaluate_expression_with_bindings(ternary.false_expr(), bindings, context);
		}
	}
	
	// For function calls (for recursion)
	if (std::holds_alternative<FunctionCallNode>(expr)) {
		const auto& func_call = std::get<FunctionCallNode>(expr);
		return evaluate_function_call_with_outer_bindings(func_call, bindings, context, &bindings);
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

	// For other expression types, use the const version (cast bindings to const)
	const std::unordered_map<std::string_view, EvalResult>& const_bindings = bindings;
	return evaluate_expression_with_bindings_const(expr_node, const_bindings, context);
}

// Original const version for backward compatibility
EvalResult Evaluator::evaluate_expression_with_bindings_const(
	const ASTNode& expr_node,
	const std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context) {
	
	if (!expr_node.is<ExpressionNode>()) {
		return EvalResult::error("Not an expression node");
	}
	
	const ExpressionNode& expr = expr_node.as<ExpressionNode>();
	
	// Check if it's an identifier that matches a parameter
	if (std::holds_alternative<IdentifierNode>(expr)) {
		const IdentifierNode& id = std::get<IdentifierNode>(expr);

		// Fast path: pre-resolved Local bindings are always in the bindings map
		if (id.binding() == IdentifierBinding::Local) {
			auto it = bindings.find(id.name());
			if (it != bindings.end()) return it->second;
			// fall through to existing logic as safety net
		}

		std::string_view name = id.name();

		// Check if it's a bound parameter
		auto it = bindings.find(name);
		if (it != bindings.end()) {
			return it->second;  // Return the bound value
		}
		
		// Not a parameter, evaluate normally
		return evaluate_identifier(id, context);
	}
	
	// For binary operators, recursively evaluate with bindings
	if (std::holds_alternative<BinaryOperatorNode>(expr)) {
		const auto& bin_op = std::get<BinaryOperatorNode>(expr);
		auto lhs_result = evaluate_expression_with_bindings_const(bin_op.get_lhs(), bindings, context);
		auto rhs_result = evaluate_expression_with_bindings_const(bin_op.get_rhs(), bindings, context);
		
		if (!lhs_result.success()) return lhs_result;
		if (!rhs_result.success()) return rhs_result;
		
		return apply_binary_op(lhs_result, rhs_result, bin_op.op());
	}
	
	// For ternary operators
	if (std::holds_alternative<TernaryOperatorNode>(expr)) {
		const auto& ternary = std::get<TernaryOperatorNode>(expr);
		auto cond_result = evaluate_expression_with_bindings_const(ternary.condition(), bindings, context);
		
		if (!cond_result.success()) return cond_result;
		
		if (cond_result.as_bool()) {
			return evaluate_expression_with_bindings_const(ternary.true_expr(), bindings, context);
		} else {
			return evaluate_expression_with_bindings_const(ternary.false_expr(), bindings, context);
		}
	}
	
	// For function calls (for recursion)
	if (std::holds_alternative<FunctionCallNode>(expr)) {
		const auto& func_call = std::get<FunctionCallNode>(expr);
		return evaluate_function_call_with_outer_bindings(func_call, bindings, context);
	}

	// For direct lambda operator() calls inside a bound constexpr context
	if (auto call_result = try_evaluate_bound_member_operator_call(expr, bindings, context)) {
		return *call_result;
	}

	if (auto member_call_result = try_evaluate_bound_member_function_call(expr, bindings, context)) {
		return *member_call_result;
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
					return EvalResult::error("Member not found in constexpr object: " + std::string(member_name));
				}
			}
		}
		// Fall through to normal evaluation for non-this member access
	}
	
	// For array subscript (e.g., arr[i] where arr is a parameter)
	if (std::holds_alternative<ArraySubscriptNode>(expr)) {
		const auto& subscript = std::get<ArraySubscriptNode>(expr);
		
		// Evaluate the index
		auto index_result = evaluate_expression_with_bindings_const(subscript.index_expr(), bindings, context);
		if (!index_result.success()) {
			return index_result;
		}
		
		long long index = index_result.as_int();
		if (index < 0) {
			return EvalResult::error("Negative array index in constant expression");
		}
		
		// Get the array expression
		const ASTNode& array_expr = subscript.array_expr();
		if (array_expr.is<ExpressionNode>()) {
			const ExpressionNode& array_expr_node = array_expr.as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(array_expr_node)) {
				std::string_view var_name = std::get<IdentifierNode>(array_expr_node).name();
				
				// Check if it's in bindings (parameter array)
				auto it = bindings.find(var_name);
				if (it != bindings.end()) {
					const EvalResult& array_result = it->second;
					if (array_result.is_array) {
						if (static_cast<size_t>(index) >= array_result.array_values.size()) {
							return EvalResult::error("Array index out of bounds in constant expression");
						}
						return EvalResult::from_int(array_result.array_values[static_cast<size_t>(index)]);
					}
					return EvalResult::error("Subscript on non-array variable in constant expression");
				}
				// Fall through to normal variable lookup
			}
		}
	}
	
	// For literals and other expressions without parameters, evaluate normally
	return evaluate(expr_node, context);
}

// Helper functions for overflow-safe arithmetic using compiler builtins
// Perform addition with overflow checking, return result or nullopt on overflow
std::optional<long long> Evaluator::safe_add(long long a, long long b) {
	long long result;
#if defined(_MSC_VER) && !defined(__clang__)
	// MSVC implementation using manual overflow detection
	if ((b > 0 && a > LLONG_MAX - b) || (b < 0 && a < LLONG_MIN - b)) {
		return std::nullopt; // Overflow
	}
	result = a + b;
	bool overflow = false;
#else
	bool overflow = __builtin_add_overflow(a, b, &result);
#endif
	return overflow ? std::nullopt : std::optional<long long>(result);
}

// Perform subtraction with overflow checking, return result or nullopt on overflow
std::optional<long long> Evaluator::safe_sub(long long a, long long b) {
	long long result;
#if defined(_MSC_VER) && !defined(__clang__)
	// MSVC implementation using manual overflow detection
	if ((b < 0 && a > LLONG_MAX + b) || (b > 0 && a < LLONG_MIN + b)) {
		return std::nullopt; // Overflow
	}
	result = a - b;
	bool overflow = false;
#else
	bool overflow = __builtin_sub_overflow(a, b, &result);
#endif
	return overflow ? std::nullopt : std::optional<long long>(result);
}

// Perform multiplication with overflow checking, return result or nullopt on overflow
std::optional<long long> Evaluator::safe_mul(long long a, long long b) {
	long long result;
#if defined(_MSC_VER) && !defined(__clang__)
	// MSVC implementation using manual overflow detection
	if (a == 0 || b == 0) {
		result = 0;
	} else if (a == LLONG_MIN || b == LLONG_MIN) {
		// Special case: LLONG_MIN * anything except 0 or 1 overflows
		if ((a == LLONG_MIN && (b < -1 || b > 1)) || (b == LLONG_MIN && (a < -1 || a > 1))) {
			return std::nullopt;
		}
		result = a * b;
	} else if ((a > 0 && b > 0 && a > LLONG_MAX / b) ||
	           (a > 0 && b < 0 && b < LLONG_MIN / a) ||
	           (a < 0 && b > 0 && a < LLONG_MIN / b) ||
	           (a < 0 && b < 0 && a < LLONG_MAX / b)) {
		return std::nullopt; // Overflow
	} else {
		result = a * b;
	}
	bool overflow = false;
#else
	bool overflow = __builtin_mul_overflow(a, b, &result);
#endif
	return overflow ? std::nullopt : std::optional<long long>(result);
}

// Perform left shift with validation and overflow checking, return result or nullopt on error
std::optional<long long> Evaluator::safe_shl(long long a, long long b) {
	if (b < 0 || b >= 64) {
		return std::nullopt; // Negative shift or shift >= bit width is undefined
	}
	if (a == 0) {
		return 0; // Shifting zero is fine
	}
	
	// Check if the shift would cause bits to be lost
	// For left shift, check if any bits would be shifted out
	long long shifted = a << b;
	long long back_shifted = shifted >> b;
	if (back_shifted != a) {
		return std::nullopt; // Overflow detected
	}
	
	return shifted;
}

// Perform right shift with validation, return result or nullopt on error
std::optional<long long> Evaluator::safe_shr(long long a, long long b) {
	if (b < 0 || b >= 64) {
		return std::nullopt; // Negative shift or shift >= bit width is undefined
	}
	return a >> b; // Right shift never overflows mathematically
}

// Helper to apply binary operators
EvalResult Evaluator::apply_binary_op(const EvalResult& lhs, const EvalResult& rhs, std::string_view op) {
	long long lhs_val = lhs.as_int();
	long long rhs_val = rhs.as_int();
	
	// Handle arithmetic operators with overflow checking
	if (op == "+") {
		if (auto result = safe_add(lhs_val, rhs_val)) {
			return EvalResult::from_int(*result);
		} else {
			return EvalResult::error("Signed integer overflow in constant expression");
		}
	} else if (op == "-") {
		if (auto result = safe_sub(lhs_val, rhs_val)) {
			return EvalResult::from_int(*result);
		} else {
			return EvalResult::error("Signed integer overflow in constant expression");
		}
	} else if (op == "*") {
		if (auto result = safe_mul(lhs_val, rhs_val)) {
			return EvalResult::from_int(*result);
		} else {
			return EvalResult::error("Signed integer overflow in constant expression");
		}
	} else if (op == "/") {
		if (rhs_val == 0) {
			return EvalResult::error("Division by zero in constant expression");
		}
		// Check for overflow in division (only happens with LLONG_MIN / -1)
		if (lhs_val == LLONG_MIN && rhs_val == -1) {
			return EvalResult::error("Signed integer overflow in constant expression");
		}
		return EvalResult::from_int(lhs_val / rhs_val);
	} else if (op == "%") {
		if (rhs_val == 0) {
			return EvalResult::error("Modulo by zero in constant expression");
		}
		return EvalResult::from_int(lhs_val % rhs_val);
	}
	
	// Handle bitwise operators
	else if (op == "&") {
		return EvalResult::from_int(lhs_val & rhs_val);
	} else if (op == "|") {
		return EvalResult::from_int(lhs_val | rhs_val);
	} else if (op == "^") {
		return EvalResult::from_int(lhs_val ^ rhs_val);
	} else if (op == "<<") {
		if (auto result = safe_shl(lhs_val, rhs_val)) {
			return EvalResult::from_int(*result);
		} else {
			return EvalResult::error("Left shift overflow or invalid shift count in constant expression");
		}
	} else if (op == ">>") {
		if (auto result = safe_shr(lhs_val, rhs_val)) {
			return EvalResult::from_int(*result);
		} else {
			return EvalResult::error("Invalid shift count in constant expression");
		}
	}
	
	// Handle comparison operators that work on integers
	if (op == "==") {
		// Compare as integers for all types
		return EvalResult::from_bool(lhs.as_int() == rhs.as_int());
	} else if (op == "!=") {
		return EvalResult::from_bool(lhs.as_int() != rhs.as_int());
	} else if (op == "<") {
		return EvalResult::from_bool(lhs.as_int() < rhs.as_int());
	} else if (op == "<=") {
		return EvalResult::from_bool(lhs.as_int() <= rhs.as_int());
	} else if (op == ">") {
		return EvalResult::from_bool(lhs.as_int() > rhs.as_int());
	} else if (op == ">=") {
		return EvalResult::from_bool(lhs.as_int() >= rhs.as_int());
	} else if (op == "&&") {
		return EvalResult::from_bool(lhs.as_bool() && rhs.as_bool());
	} else if (op == "||") {
		return EvalResult::from_bool(lhs.as_bool() || rhs.as_bool());
	}

	// Unsupported operator
	return EvalResult::error("Operator '" + std::string(op) + "' not supported in constant expressions");
}

EvalResult Evaluator::apply_unary_op(const EvalResult& operand, std::string_view op) {
	if (op == "!") {
		return EvalResult::from_bool(!operand.as_bool());
	} else if (op == "~") {
		return EvalResult::from_int(~operand.as_int());
	} else if (op == "-") {
		// Unary minus - negate the value
		if (std::holds_alternative<double>(operand.value)) {
			return EvalResult::from_double(-operand.as_double());
		}
		// Check for overflow: negating LLONG_MIN overflows
		long long val = operand.as_int();
		if (val == LLONG_MIN) {
			return EvalResult::error("Signed integer overflow in unary minus");
		}
		return EvalResult::from_int(-val);
	} else if (op == "+") {
		// Unary plus - no-op, just return the value
		return operand;
	}

	// Unsupported operator
	return EvalResult::error("Unary operator '" + std::string(op) + "' not supported in constant expressions");
}

// Evaluate qualified identifier (e.g., Namespace::var or Template<T>::member)
EvalResult Evaluator::evaluate_qualified_identifier(const QualifiedIdentifierNode& qualified_id, EvaluationContext& context) {
	// Look up the qualified name in the symbol table
	if (!context.symbols) {
		return EvalResult::error("Cannot evaluate qualified identifier: no symbol table provided");
	}

	// Try to look up the qualified name
	auto symbol_opt = context.symbols->lookup_qualified(qualified_id.qualifiedIdentifier());
	if (!symbol_opt.has_value()) {
		// PHASE 3 FIX: If not found in symbol table, try looking up as struct static member
		// This handles cases like is_pointer_impl<int*>::value where value is a static member
		// Also handles type aliases like `using my_true = integral_constant<bool, true>; my_true::value`
		NamespaceHandle ns_handle = qualified_id.namespace_handle();
		StringHandle struct_handle;
		
		if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
			FLASH_LOG(ConstExpr, Debug, "ns_handle.isGlobal()=", ns_handle.isGlobal(),
			          ", qualified_id='", qualified_id.full_name(), "'");
		}
		
		if (!ns_handle.isGlobal()) {
			struct_handle = gNamespaceRegistry.getQualifiedNameHandle(ns_handle);
			if (!struct_handle.isValid()) {
				struct_handle = StringTable::getOrInternStringHandle(gNamespaceRegistry.getName(ns_handle));
			}
		}
		
		// If we still don't have a struct name, derive it from the qualified identifier.
		// Example: "std::is_integral<int>::value" -> "std::is_integral<int>"
		if (!struct_handle.isValid()) {
			std::string_view ns_name = gNamespaceRegistry.getQualifiedName(ns_handle);
			if (!ns_name.empty()) {
				struct_handle = StringTable::getOrInternStringHandle(ns_name);
				if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
					FLASH_LOG(ConstExpr, Debug, "Extracted struct_name='", ns_name, "' from qualified namespace");
				}
			}
		}
		
		if (struct_handle.isValid()) {
			if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
				FLASH_LOG(ConstExpr, Debug, "Looking up struct '", StringTable::getStringView(struct_handle), "' for member '", qualified_id.name(), "'");
			}
			
			// Look up the struct in gTypesByName
			auto struct_type_it = gTypesByName.find(struct_handle);
			
			// If not found with the full qualified name (e.g., "std::is_integral$hash"),
			// try without the namespace prefix (e.g., "is_integral$hash") since template
			// instantiations are often registered with just the short name
			if (struct_type_it == gTypesByName.end()) {
				std::string_view full_name = StringTable::getStringView(struct_handle);
				size_t last_colon = full_name.rfind("::");
				if (last_colon != std::string_view::npos) {
					std::string_view short_name = full_name.substr(last_colon + 2);
					StringHandle short_handle = StringTable::getOrInternStringHandle(short_name);
					struct_type_it = gTypesByName.find(short_handle);
					if (struct_type_it != gTypesByName.end()) {
						FLASH_LOG(ConstExpr, Debug, "Found type using short name '", short_name, "'");
					}
				}
			}
			
			// If not found directly, this might be a type alias
			// Type aliases are registered with their alias name pointing to the underlying type
			const StructTypeInfo* struct_info = nullptr;
			const TypeInfo* resolved_type_info = nullptr;
			
			if (struct_type_it != gTypesByName.end()) {
				const TypeInfo* type_info = struct_type_it->second;
				
				if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
					FLASH_LOG(ConstExpr, Debug, "Found type_info, isStruct=", type_info->isStruct(),
					          ", type_index=", type_info->type_index_, ", hasStructInfo=", (type_info->getStructInfo() != nullptr));
				}
				
				// Follow the type alias chain until we find a struct with actual StructTypeInfo
				// Type aliases may have isStruct()=true but getStructInfo()=null
				// Limit iterations to prevent infinite loops from cycles
				constexpr size_t MAX_ALIAS_CHAIN_DEPTH = 100;
				size_t alias_depth = 0;
				while (type_info && type_info->type_index_ > 0 && type_info->type_index_ < gTypeInfo.size() && alias_depth < MAX_ALIAS_CHAIN_DEPTH) {
					// Check if we already have StructInfo - if so, we're done
					if (type_info->isStruct() && type_info->getStructInfo() != nullptr) {
						break;
					}
					// Follow the type_index_ to find the underlying type
					const TypeInfo& underlying = gTypeInfo[type_info->type_index_];
					if (&underlying == type_info) break;  // Avoid direct self-reference
					if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
						FLASH_LOG(ConstExpr, Debug, "Following type alias to index ", type_info->type_index_);
					}
					type_info = &underlying;
					++alias_depth;
				}
				
				if (type_info && type_info->isStruct()) {
					struct_info = type_info->getStructInfo();
					resolved_type_info = type_info;
				}
			}
			
			// If still not found, try resolving by checking if there's a type alias in gTypeInfo
			// Note: This linear search is a fallback for edge cases; primary lookup uses gTypesByName
			if (!struct_info) {
				// Try looking up by iterating through gTypeInfo to find a type with matching name
				for (const auto& type_info : gTypeInfo) {
					if (type_info.isStruct()) {
						const StructTypeInfo* si = type_info.getStructInfo();
						if (si && si->name == struct_handle) {
							struct_info = si;
							resolved_type_info = &type_info;
							break;
						}
					}
				}
			}
			
			if (struct_info) {
				// Look for static member recursively (checks base classes too)
				StringHandle member_handle = StringTable::getOrInternStringHandle(qualified_id.name());
				if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
					FLASH_LOG(ConstExpr, Debug, "Static lookup in struct '", StringTable::getStringView(struct_handle), "', bases=", struct_info->base_classes.size());
					if (resolved_type_info) {
						FLASH_LOG(ConstExpr, Debug, "Resolved type base template='", StringTable::getStringView(resolved_type_info->baseTemplateName()), "', template args=", resolved_type_info->template_args_.size());
						for (size_t i = 0; i < resolved_type_info->template_args_.size(); ++i) {
							const auto& arg = resolved_type_info->template_args_[i];
							FLASH_LOG(ConstExpr, Debug, "  resolved arg[", i, "] is_value=", arg.is_value, ", base_type=", static_cast<int>(arg.base_type), ", type_index=", arg.type_index, ", value(int)=", arg.intValue());
						}
					}
					for (const auto& base : struct_info->base_classes) {
						if (base.type_index < gTypeInfo.size()) {
							FLASH_LOG(ConstExpr, Debug, "  base type_index=", base.type_index, " name='", StringTable::getStringView(gTypeInfo[base.type_index].name_), "'");
						}
					}
					FLASH_LOG(ConstExpr, Debug, "  static members=", struct_info->static_members.size(), ", non-static members=", struct_info->members.size());
					for (const auto& static_member : struct_info->static_members) {
						FLASH_LOG(ConstExpr, Debug, "    static member name='", StringTable::getStringView(static_member.getName()), "'");
					}
					for (const auto& member : struct_info->members) {
						FLASH_LOG(ConstExpr, Debug, "    member name='", StringTable::getStringView(member.name), "'");
					}
				}
				
				auto traitKindFromTemplateName = [](StringHandle template_name) -> std::optional<TypeTraitKind> {
					std::string_view name = StringTable::getStringView(template_name);
					if (name == "is_void") return TypeTraitKind::IsVoid;
					if (name == "is_null_pointer" || name == "is_nullptr") return TypeTraitKind::IsNullptr;
					if (name == "is_integral") return TypeTraitKind::IsIntegral;
					if (name == "is_floating_point") return TypeTraitKind::IsFloatingPoint;
					if (name == "is_array") return TypeTraitKind::IsArray;
					if (name == "is_pointer") return TypeTraitKind::IsPointer;
					if (name == "is_lvalue_reference") return TypeTraitKind::IsLvalueReference;
					if (name == "is_rvalue_reference") return TypeTraitKind::IsRvalueReference;
					if (name == "is_member_object_pointer") return TypeTraitKind::IsMemberObjectPointer;
					if (name == "is_member_function_pointer") return TypeTraitKind::IsMemberFunctionPointer;
					if (name == "is_enum") return TypeTraitKind::IsEnum;
					if (name == "is_union") return TypeTraitKind::IsUnion;
					if (name == "is_class") return TypeTraitKind::IsClass;
					if (name == "is_function") return TypeTraitKind::IsFunction;
					if (name == "is_reference") return TypeTraitKind::IsReference;
					if (name == "is_arithmetic") return TypeTraitKind::IsArithmetic;
					if (name == "is_fundamental") return TypeTraitKind::IsFundamental;
					if (name == "is_object") return TypeTraitKind::IsObject;
					if (name == "is_scalar") return TypeTraitKind::IsScalar;
					if (name == "is_compound") return TypeTraitKind::IsCompound;
					if (name == "is_const") return TypeTraitKind::IsConst;
					if (name == "is_volatile") return TypeTraitKind::IsVolatile;
					if (name == "is_signed") return TypeTraitKind::IsSigned;
					if (name == "is_unsigned") return TypeTraitKind::IsUnsigned;
					if (name == "is_bounded_array") return TypeTraitKind::IsBoundedArray;
					if (name == "is_unbounded_array") return TypeTraitKind::IsUnboundedArray;
					return std::nullopt;
				};

				struct TraitInput {
					Type base_type;
					TypeIndex type_index;
					size_t pointer_depth;
					ReferenceQualifier ref_qualifier;
					CVQualifier cv;
					bool is_array;
					std::optional<size_t> array_size;
					const TypeInfo* type_info;
					const StructTypeInfo* struct_info;
				};

				auto evaluateTypeTraitFromInput = [](TypeTraitKind trait_kind, const TraitInput& input) {
					return evaluateTypeTrait(trait_kind, input.base_type, input.type_index,
						input.ref_qualifier != ReferenceQualifier::None,
						input.ref_qualifier == ReferenceQualifier::RValueReference,
						input.ref_qualifier == ReferenceQualifier::LValueReference,
						input.pointer_depth, input.cv, input.is_array, input.array_size, input.type_info, input.struct_info);
				};

				auto evaluate_unary_trait_from_resolved = [&](StringHandle trait_template_name) -> std::optional<EvalResult> {
					if (!resolved_type_info || resolved_type_info->template_args_.empty()) {
						return std::nullopt;
					}
					
					auto trait_kind = traitKindFromTemplateName(trait_template_name);
					if (!trait_kind.has_value()) {
						return std::nullopt;
					}
					
					const auto& arg_info = resolved_type_info->template_args_[0];
					TraitInput input{
						.base_type = arg_info.base_type,
						.type_index = arg_info.type_index,
						.pointer_depth = arg_info.pointer_depth ? arg_info.pointer_depth : arg_info.pointer_cv_qualifiers.size(),
						.ref_qualifier = arg_info.ref_qualifier,
						.cv = arg_info.cv_qualifier,
						.is_array = arg_info.is_array,
						.array_size = arg_info.array_size,
						.type_info = nullptr,
						.struct_info = nullptr
					};
					
					if (input.type_index > 0 && input.type_index < gTypeInfo.size()) {
						input.type_info = &gTypeInfo[input.type_index];
						input.base_type = input.type_info->type_;
						input.pointer_depth = input.type_info->pointer_depth_;
						input.ref_qualifier = input.type_info->reference_qualifier_;
						input.struct_info = input.type_info->getStructInfo();
					}
					
					auto trait_result = evaluateTypeTraitFromInput(*trait_kind, input);
					if (trait_result.success) {
						return trait_result.value ? EvalResult::from_bool(true) : EvalResult::from_bool(false);
					}
					
					return std::nullopt;
				};
				auto evaluate_integral_constant_value = [](const TypeInfo& ti) -> std::optional<EvalResult> {
					if (!ti.isTemplateInstantiation()) {
						return std::nullopt;
					}
					
					const auto& args = ti.templateArgs();
					if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
						FLASH_LOG(ConstExpr, Debug, "Integral constant synthesis: template args=", args.size());
						for (size_t i = 0; i < args.size(); ++i) {
							FLASH_LOG(ConstExpr, Debug, "  arg[", i, "] is_value=", args[i].is_value, ", base_type=", static_cast<int>(args[i].base_type), ", type_index=", args[i].type_index, ", value(int)=", args[i].intValue());
						}
					}
					if (args.size() < 2) {
						FLASH_LOG(ConstExpr, Debug, "Integral constant synthesis failed: expected >=2 template args, got ", args.size());
						return std::nullopt;
					}
					
					const auto& value_arg = args[1];
					if (!value_arg.is_value) {
						FLASH_LOG(ConstExpr, Debug, "Integral constant synthesis failed: value arg is not non-type value");
						return std::nullopt;
					}
					
					// Convert the stored value based on the template argument type
					switch (value_arg.base_type) {
						case Type::Bool:
							return EvalResult::from_bool(value_arg.intValue() != 0);
						case Type::UnsignedChar:
						case Type::UnsignedShort:
						case Type::UnsignedInt:
						case Type::UnsignedLong:
						case Type::UnsignedLongLong:
							return EvalResult::from_uint(static_cast<unsigned long long>(value_arg.intValue()));
						default:
							return EvalResult::from_int(value_arg.intValue());
					}
				};
				
				auto [static_member, owner_struct] = struct_info->findStaticMemberRecursive(member_handle);
				
				if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
					FLASH_LOG(ConstExpr, Debug, "Static member found: ", (static_member != nullptr),
					          ", owner: ", (owner_struct != nullptr));
				}
				
				// Fallback: synthesize integral_constant::value from template arguments when static member isn't registered
				const StringHandle value_handle = StringTable::getOrInternStringHandle("value");
				if (!static_member && member_handle == value_handle) {
					if (resolved_type_info) {
						if (auto trait_value = evaluate_unary_trait_from_resolved(resolved_type_info->baseTemplateName())) {
							FLASH_LOG(ConstExpr, Debug, "Synthesized value from unary trait evaluator for ", StringTable::getStringView(resolved_type_info->baseTemplateName()));
							return *trait_value;
						}
					}
					if (resolved_type_info) {
						if (auto synthesized = evaluate_integral_constant_value(*resolved_type_info)) {
							FLASH_LOG(ConstExpr, Debug, "Synthesized integral_constant value from template args (self)");
							return *synthesized;
						}
					}
					for (const auto& base : struct_info->base_classes) {
						if (base.type_index < gTypeInfo.size()) {
							if (auto synthesized = evaluate_integral_constant_value(gTypeInfo[base.type_index])) {
								FLASH_LOG(ConstExpr, Debug, "Synthesized integral_constant value from base template args");
								return *synthesized;
							}
						}
					}
				}
			
				if (static_member && owner_struct) {
					FLASH_LOG(ConstExpr, Debug, "Static member is_const: ", static_member->is_const(), 
					          ", has_initializer: ", static_member->initializer.has_value());
					
					// Always try to trigger lazy instantiation for static members.
					// The member might have an initializer from template parsing that
					// still contains unsubstituted template parameters (like _R1::num).
					// The lazy system will substitute them and update the initializer.
					// If the member is not in the lazy registry, this is a fast no-op.
					if (context.parser != nullptr) {
						bool did_lazy = context.parser->instantiateLazyStaticMember(
							owner_struct->name, member_handle);
						if (did_lazy) {
							// Re-lookup the static member after instantiation
							auto relookup_result = struct_info->findStaticMemberRecursive(member_handle);
							if (relookup_result.first && relookup_result.first->initializer.has_value()) {
								FLASH_LOG(ConstExpr, Debug, "After lazy instantiation, evaluating initializer");
								return evaluate(relookup_result.first->initializer.value(), context);
							}
						}
					}
					
					// Found a static member - evaluate its initializer if available
					// Note: Even if not marked const, we can evaluate constexpr initializers
					if (static_member->initializer.has_value()) {
						FLASH_LOG(ConstExpr, Debug, "Evaluating static member initializer");
					}
					
					// If not constexpr or no initializer, return default value based on type
					FLASH_LOG(ConstExpr, Debug, "Returning default value for type: ", static_cast<int>(static_member->type));
					return evaluate_static_member_initializer_or_default(*static_member, context);
				}
			}
		}
		
		// Not found in symbol table or as struct static member
		// Check if this looks like a template instantiation with dependent arguments
		// Pattern: __template_name__DependentArg::member
		// Work with string_view to avoid unnecessary copies
		std::string_view ns_name = gNamespaceRegistry.getQualifiedName(qualified_id.namespace_handle());
		std::string_view member_name = qualified_id.name();
		
		// Check if the namespace part looks like a template instantiation (contains template argument separator)
		// Template names with arguments get mangled as template_name_arg1_arg2...
		// If any argument is a template parameter (starts with _ often), it's template-dependent
		// BUT: Don't treat names like "is_integral_int" as dependent - "int" is a concrete type!
		// Only treat as dependent if it contains identifiers that START with underscore (like _Tp, _Up)
		// or have double underscores (like __type_parameter)
		bool looks_dependent = false;
		if (!ns_name.empty() && ns_name.find('_') != std::string_view::npos) {
			// Check if any component looks like a template parameter (starts with _ or __)
			// Split by underscore and check each part
			for (size_t i = 0; i < ns_name.size(); ) {
				if (ns_name[i] == '_') {
					// Found underscore - check if next char is also underscore or uppercase
					// Template parameters often look like: _Tp, _Up, _T, __type, etc.
					if (i + 1 < ns_name.size()) {
						char next = ns_name[i + 1];
						if (next == '_' || std::isupper(static_cast<unsigned char>(next))) {
							looks_dependent = true;
							break;
						}
					}
				}
				++i;
			}
		}
		
		if (looks_dependent && context.parser != nullptr) {
			// This might be a template instantiation with dependent arguments
			// Treat it as template-dependent
			return EvalResult::error("Template instantiation with dependent arguments in constant expression: " + 
			                         std::string(ns_name) + "::" + std::string(member_name),
			                         EvalErrorType::TemplateDependentExpression);
		}
		
		// Not found in symbol table or as struct static member
		return EvalResult::error("Undefined qualified identifier in constant expression: " + qualified_id.full_name());
	}

	const ASTNode& symbol_node = *symbol_opt;

	// Check if it's a variable declaration (constexpr)
	if (symbol_node.is<VariableDeclarationNode>()) {
		const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
		if (!var_decl.is_constexpr()) {
			return EvalResult::error("Qualified variable must be constexpr: " + qualified_id.full_name());
		}
		const auto& initializer = var_decl.initializer();
		if (!initializer.has_value()) {
			return EvalResult::error("Constexpr variable has no initializer: " + qualified_id.full_name());
		}

		return evaluate(initializer.value(), context);
	}

	// Could be other types like enum constants - add support as needed
	return EvalResult::error("Qualified identifier is not a constant expression: " + qualified_id.full_name());
}

// Evaluate member access (e.g., obj.member or struct_type::static_member)
// Also supports nested member access (e.g., obj.inner.value)
EvalResult Evaluator::evaluate_member_access(const MemberAccessNode& member_access, EvaluationContext& context) {
	// Get the object expression (e.g., 'p1' in 'p1.x')
	const ASTNode& object_expr = member_access.object();
	std::string_view member_name = member_access.member_name();
	
	// Check if this is a nested member access (e.g., obj.inner.value)
	if (object_expr.is<ExpressionNode>()) {
		const ExpressionNode& expr_node = object_expr.as<ExpressionNode>();
		if (std::holds_alternative<MemberAccessNode>(expr_node)) {
			// Nested member access - first get the intermediate struct initializer
			const MemberAccessNode& inner_access = std::get<MemberAccessNode>(expr_node);
			return evaluate_nested_member_access(inner_access, member_name, context);
		}
	}
	
	// For constexpr struct member access, we need to handle the case where:
	// - The object is an identifier referencing a constexpr variable
	// - The variable is initialized with a ConstructorCallNode
	// - We need to find the constructor declaration and its member initializer list
	// - Extract the member value from the initializer expression
	
	const IdentifierNode* object_identifier = nullptr;
	if (object_expr.is<ExpressionNode>()) {
		const ExpressionNode& expr_node = object_expr.as<ExpressionNode>();
		if (std::holds_alternative<IdentifierNode>(expr_node)) {
			object_identifier = &std::get<IdentifierNode>(expr_node);
		}
	} else if (object_expr.is<IdentifierNode>()) {
		object_identifier = &object_expr.as<IdentifierNode>();
	}

	// The object might be wrapped in an ExpressionNode, so unwrap it
	std::string_view var_name = getIdentifierNameFromAstNode(object_expr);
	if (var_name.empty()) {
		if (object_expr.is<ExpressionNode>()) {
			const ExpressionNode& expr_node = object_expr.as<ExpressionNode>();
			// Check for ArraySubscriptNode
			if (std::holds_alternative<ArraySubscriptNode>(expr_node)) {
				// Array subscript on struct - evaluate array element then access member
				return evaluate_array_subscript_member_access(std::get<ArraySubscriptNode>(expr_node), member_name, context);
			}
			// Check for FunctionCallNode - evaluate the return type and access static member
			if (std::holds_alternative<FunctionCallNode>(expr_node)) {
				const FunctionCallNode& func_call = std::get<FunctionCallNode>(expr_node);
				return evaluate_function_call_member_access(func_call, member_name, context);
			}
		}
		return EvalResult::error("Complex member access expressions not yet supported in constant expressions");
	}
	
	ResolvedConstexprObject resolved_object;
	if (auto resolve_error = resolve_constexpr_object_source(
		object_identifier,
		var_name,
		context,
		"member access",
		resolved_object)) {
		return *resolve_error;
	}

	if (resolved_object.initializer == nullptr) {
		return EvalResult::error("Internal error: unresolved member access object source");
	}

	const VariableDeclarationNode* var_decl = resolved_object.var_decl;
	TypeIndex var_type_index = resolved_object.declared_type_index;
	
	// Before checking if the variable is constexpr, check if we're accessing a static member
	// Static members can be accessed through any instance (even non-constexpr)
	// because they don't depend on the instance
	
	if (var_decl) {
		const DeclarationNode& var_declaration = var_decl->declaration();
		const ASTNode& var_type_node = var_declaration.type_node();
		if (var_type_node.is<TypeSpecifierNode>()) {
			const TypeSpecifierNode& var_type_spec = var_type_node.as<TypeSpecifierNode>();
			var_type_index = var_type_spec.type_index();
			
			if (var_type_index != TypeIndex{0} && var_type_index < gTypeInfo.size()) {
				const TypeInfo& var_type_info = gTypeInfo[var_type_index];
				const StructTypeInfo* struct_info = var_type_info.getStructInfo();
				
				if (struct_info) {
					// Look for static member
					StringHandle member_handle = StringTable::getOrInternStringHandle(member_name);
					auto [static_member, owner_struct] = struct_info->findStaticMemberRecursive(member_handle);

					if (static_member && owner_struct) {
						FLASH_LOG(ConstExpr, Debug, "Accessing static member through instance: ", member_name);

						// Found a static member - evaluate its initializer if available
						return evaluate_static_member_initializer_or_default(*static_member, context);
					}
				}
			}
		}
	
		// If not a static member access, check if it's a constexpr variable
		if (!var_decl->is_constexpr()) {
			return EvalResult::error("Variable in member access must be constexpr: " + std::string(var_name));
		}
	}
	
	ResolvedConstexprMemberSource resolved_member;
	if (auto member_error = resolve_constexpr_member_source_from_initializer(
		*resolved_object.initializer,
		var_type_index,
		member_name,
		"member access",
		context,
		resolved_member)) {
		return *member_error;
	}

	if (!resolved_member.initializer.has_value()) {
		return EvalResult::error("Internal error: unresolved member source in member access");
	}

	if (!resolved_member.evaluation_bindings.empty()) {
		return evaluate_expression_with_bindings(
			resolved_member.initializer.value(),
			resolved_member.evaluation_bindings,
			context);
	}

	return evaluate(resolved_member.initializer.value(), context);
}


std::optional<EvalResult> Evaluator::resolve_constexpr_member_source_from_initializer(
	const std::optional<ASTNode>& object_initializer,
	TypeIndex declared_type_index,
	std::string_view member_name,
	std::string_view usage_name,
	EvaluationContext& context,
	ResolvedConstexprMemberSource& resolved_member,
	const std::unordered_map<std::string_view, EvalResult>* enclosing_bindings) {
	resolved_member = {};

	if (!object_initializer.has_value()) {
		return EvalResult::error("Constexpr " + std::string(usage_name) + " object has no initializer");
	}

	StringHandle member_name_handle = StringTable::getOrInternStringHandle(member_name);
	auto find_member_info = [member_name_handle](const StructTypeInfo* struct_info) -> const StructMember* {
		if (!struct_info) {
			return nullptr;
		}

		for (const auto& member : struct_info->members) {
			if (member.getName() == member_name_handle) {
				return &member;
			}
		}

		return nullptr;
	};

	auto resolve_explicit_member = [&](const StructTypeInfo* struct_info, const ASTNode* explicit_initializer) -> std::optional<EvalResult> {
		const StructMember* member_info = find_member_info(struct_info);
		if (!member_info) {
			return EvalResult::error("Member '" + std::string(member_name) + "' not found in " + std::string(usage_name));
		}

		resolved_member.member_info = member_info;
		if (explicit_initializer) {
			resolved_member.initializer = *explicit_initializer;
			if (enclosing_bindings) {
				resolved_member.evaluation_bindings = *enclosing_bindings;
			}
			return std::nullopt;
		}

		if (member_info->default_initializer.has_value()) {
			resolved_member.initializer = member_info->default_initializer.value();
			resolved_member.evaluation_bindings.clear();
			return std::nullopt;
		}

		return EvalResult::error("Member '" + std::string(member_name) + "' not found in " + std::string(usage_name) + " and has no default value");
	};

	const ASTNode& initializer = object_initializer.value();
	if (initializer.is<InitializerListNode>()) {
		if (declared_type_index == TypeIndex{0} || declared_type_index >= gTypeInfo.size()) {
			return EvalResult::error("Invalid type index in " + std::string(usage_name));
		}

		const StructTypeInfo* struct_info = gTypeInfo[declared_type_index].getStructInfo();
		if (!struct_info) {
			return EvalResult::error("Aggregate-initialized constexpr object is not a struct in " + std::string(usage_name));
		}

		const InitializerListNode& init_list = initializer.as<InitializerListNode>();
		size_t positional_member_index = 0;
		for (size_t init_index = 0; init_index < init_list.size(); ++init_index) {
			StringHandle current_member_name;
			if (init_list.is_designated(init_index)) {
				current_member_name = init_list.member_name(init_index);
			} else if (positional_member_index < struct_info->members.size()) {
				current_member_name = struct_info->members[positional_member_index].getName();
				positional_member_index++;
			} else {
				break;
			}

			if (current_member_name == member_name_handle) {
				return resolve_explicit_member(struct_info, &init_list.initializers()[init_index]);
			}
		}

		return resolve_explicit_member(struct_info, nullptr);
	}

	const ConstructorCallNode* ctor_call_ptr = extract_constructor_call(object_initializer);
	if (!ctor_call_ptr) {
		return EvalResult::error("Constexpr " + std::string(usage_name) + " requires a struct initializer");
	}

	const ConstructorCallNode& ctor_call = *ctor_call_ptr;
	const ASTNode& type_node = ctor_call.type_node();
	if (!type_node.is<TypeSpecifierNode>()) {
		return EvalResult::error("Constructor call without valid type specifier");
	}

	const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
	if (type_spec.type() != Type::Struct && type_spec.type() != Type::UserDefined) {
		return EvalResult::error("Constexpr " + std::string(usage_name) + " requires a struct type");
	}

	TypeIndex type_index = type_spec.type_index();
	if (type_index >= gTypeInfo.size()) {
		return EvalResult::error("Invalid type index in " + std::string(usage_name));
	}

	const StructTypeInfo* struct_info = gTypeInfo[type_index].getStructInfo();
	if (!struct_info) {
		return EvalResult::error("Type is not a struct in " + std::string(usage_name));
	}

	const auto& ctor_args = ctor_call.arguments();
	const ConstructorDeclarationNode* matching_ctor = find_matching_constructor_by_parameter_count(struct_info, ctor_args.size());
	if (!matching_ctor) {
		return EvalResult::error("No matching constructor found for constexpr " + std::string(usage_name));
	}

	const ASTNode* member_initializer = nullptr;
	for (const auto& mem_init : matching_ctor->member_initializers()) {
		if (mem_init.member_name == member_name) {
			member_initializer = &mem_init.initializer_expr;
			break;
		}
	}

	if (auto resolve_error = resolve_explicit_member(struct_info, member_initializer)) {
		return resolve_error;
	}

	if (member_initializer == nullptr) {
		return std::nullopt;
	}

	const auto& params = matching_ctor->parameter_nodes();
	auto bind_result = bind_evaluated_arguments(
		params,
		ctor_args,
		resolved_member.evaluation_bindings,
		context,
		"Invalid parameter node in constexpr member source constructor",
		enclosing_bindings,
		true);
	if (!bind_result.success()) {
		return bind_result;
	}

	return std::nullopt;
}

// Helper to get StructTypeInfo from a TypeSpecifierNode
const StructTypeInfo* Evaluator::get_struct_info_from_type(const TypeSpecifierNode& type_spec) {
	if (type_spec.type() != Type::Struct && type_spec.type() != Type::UserDefined) {
		return nullptr;
	}
	
	TypeIndex type_index = type_spec.type_index();
	if (type_index >= gTypeInfo.size()) {
		return nullptr;
	}
	
	const TypeInfo& type_info = gTypeInfo[type_index];
	return type_info.getStructInfo();
}

std::optional<EvalResult> Evaluator::resolve_constexpr_object_source(
	const IdentifierNode* object_identifier,
	std::string_view object_name,
	EvaluationContext& context,
	std::string_view usage_name,
	ResolvedConstexprObject& resolved_object) {
	resolved_object = {};

	if (auto static_member_result = resolve_current_struct_static_member(
		object_identifier,
		context,
		CurrentStructStaticLookupMode::BoundOnly);
		static_member_result.static_member) {
		resolved_object.initializer = &static_member_result.static_member->initializer;
		resolved_object.declared_type_index = static_member_result.static_member->type_index;
		return std::nullopt;
	}

	if (!context.symbols) {
		return EvalResult::error("Cannot evaluate " + std::string(usage_name) + ": no symbol table provided");
	}

	std::optional<ASTNode> symbol_opt = lookup_identifier_symbol(object_identifier, object_name, *context.symbols);
	if (!symbol_opt.has_value()) {
		return EvalResult::error("Undefined variable in " + std::string(usage_name) + ": " + std::string(object_name));
	}

	const ASTNode& symbol_node = symbol_opt.value();
	if (!symbol_node.is<VariableDeclarationNode>()) {
		return EvalResult::error("Identifier in " + std::string(usage_name) + " is not a variable: " + std::string(object_name));
	}

	resolved_object.var_decl = &symbol_node.as<VariableDeclarationNode>();
	resolved_object.initializer = &resolved_object.var_decl->initializer();

	const DeclarationNode& decl = resolved_object.var_decl->declaration();
	if (decl.type_node().is<TypeSpecifierNode>()) {
		resolved_object.declared_type_index = decl.type_node().as<TypeSpecifierNode>().type_index();
	}

	return std::nullopt;
}

// Evaluate nested member access (e.g., obj.inner.value)
EvalResult Evaluator::evaluate_nested_member_access(
	const MemberAccessNode& inner_access,
	std::string_view final_member_name,
	EvaluationContext& context) {
	
	// First, we need to get the base object and the chain of member accesses
	// For obj.inner.value:
	// - inner_access.object() is 'obj' (identifier)
	// - inner_access.member_name() is 'inner'
	// - final_member_name is 'value'
	
	const ASTNode& base_obj_expr = inner_access.object();
	std::string_view intermediate_member = inner_access.member_name();
	
	// Get the base variable name
	std::string_view base_var_name;
	const IdentifierNode* base_identifier = nullptr;
	
	// Handle deeper nesting recursively
	if (base_obj_expr.is<ExpressionNode>()) {
		const ExpressionNode& expr_node = base_obj_expr.as<ExpressionNode>();
		if (std::holds_alternative<MemberAccessNode>(expr_node)) {
			// Even deeper nesting - this requires more complex logic
			// For now, we support up to one level of nesting
			return EvalResult::error("Deeply nested member access (more than 2 levels) not yet supported");
		}
		if (!std::holds_alternative<IdentifierNode>(expr_node)) {
			return EvalResult::error("Complex base expression in nested member access not supported");
		}
		base_identifier = &std::get<IdentifierNode>(expr_node);
		base_var_name = base_identifier->name();
	} else if (base_obj_expr.is<IdentifierNode>()) {
		base_identifier = &base_obj_expr.as<IdentifierNode>();
		base_var_name = base_identifier->name();
	} else {
		return EvalResult::error("Invalid base expression in nested member access");
	}

	ResolvedConstexprObject resolved_object;
	if (auto resolve_error = resolve_constexpr_object_source(
		base_identifier,
		base_var_name,
		context,
		"nested member access",
		resolved_object)) {
		return *resolve_error;
	}

	if (resolved_object.initializer == nullptr) {
		return EvalResult::error("Internal error: unresolved nested member access object source");
	}

	if (resolved_object.var_decl && !resolved_object.var_decl->is_constexpr()) {
		return EvalResult::error("Variable in nested member access must be constexpr");
	}

	const std::optional<ASTNode>* initializer = resolved_object.initializer;
	TypeIndex base_declared_type_index = resolved_object.declared_type_index;

	if (!initializer->has_value()) {
		return EvalResult::error("Constexpr variable has no initializer in nested member access");
	}
	
	ResolvedConstexprMemberSource intermediate_member_source;
	if (auto resolve_error = resolve_constexpr_member_source_from_initializer(
		*initializer,
		base_declared_type_index,
		intermediate_member,
		"nested member access",
		context,
		intermediate_member_source)) {
		return *resolve_error;
	}

	if (!intermediate_member_source.initializer.has_value() || !intermediate_member_source.member_info) {
		return EvalResult::error("Internal error: unresolved intermediate member source in nested member access");
	}

	const StructMember* intermediate_member_info = intermediate_member_source.member_info;
	if (intermediate_member_info->type != Type::Struct && intermediate_member_info->type != Type::UserDefined) {
		return EvalResult::error("Intermediate member is not a struct type");
	}

	TypeIndex inner_type_index = intermediate_member_info->type_index;
	const auto* intermediate_bindings = intermediate_member_source.evaluation_bindings.empty()
		? nullptr
		: &intermediate_member_source.evaluation_bindings;

	if (intermediate_member_source.initializer->is<InitializerListNode>() ||
		extract_constructor_call(intermediate_member_source.initializer)) {
		ResolvedConstexprMemberSource final_member_source;
		if (auto final_error = resolve_constexpr_member_source_from_initializer(
			intermediate_member_source.initializer,
			inner_type_index,
			final_member_name,
			"nested member access",
			context,
			final_member_source,
			intermediate_bindings)) {
			return *final_error;
		}

		if (!final_member_source.initializer.has_value()) {
			return EvalResult::error("Internal error: unresolved final member source in nested member access");
		}

		if (!final_member_source.evaluation_bindings.empty()) {
			return evaluate_expression_with_bindings(
				final_member_source.initializer.value(),
				final_member_source.evaluation_bindings,
				context);
		}

		return evaluate(final_member_source.initializer.value(), context);
	}

	if (inner_type_index >= gTypeInfo.size()) {
		return EvalResult::error("Invalid inner type index");
	}

	const StructTypeInfo* inner_struct_info = gTypeInfo[inner_type_index].getStructInfo();
	if (!inner_struct_info) {
		return EvalResult::error("Inner member type is not a struct");
	}

	const ASTNode& intermediate_init = intermediate_member_source.initializer.value();
	if ((*initializer)->is<InitializerListNode>()) {
		StringHandle final_handle = StringTable::getOrInternStringHandle(final_member_name);
		if (!inner_struct_info->members.empty() && inner_struct_info->members[0].getName() == final_handle) {
			if (intermediate_bindings) {
				return evaluate_expression_with_bindings_const(intermediate_init, *intermediate_bindings, context);
			}
			return evaluate(intermediate_init, context);
		}

		return EvalResult::error("Final member '" + std::string(final_member_name) +
			"' not reachable via scalar initializer (brace elision) in nested aggregate");
	}

	EvalResult init_arg_result = intermediate_bindings
		? evaluate_expression_with_bindings_const(intermediate_init, *intermediate_bindings, context)
		: evaluate(intermediate_init, context);
	if (!init_arg_result.success()) {
		return init_arg_result;
	}

	const ConstructorDeclarationNode* inner_matching_ctor = nullptr;
	for (const auto& member_func : inner_struct_info->member_functions) {
		if (!member_func.is_constructor) continue;
		if (!member_func.function_decl.is<ConstructorDeclarationNode>()) continue;
		const ConstructorDeclarationNode& ctor = member_func.function_decl.as<ConstructorDeclarationNode>();
		if (ctor.parameter_nodes().size() == 1) {
			inner_matching_ctor = &ctor;
			break;
		}
	}

	if (!inner_matching_ctor) {
		return EvalResult::error("No matching single-argument constructor for inner struct");
	}

	std::unordered_map<std::string_view, EvalResult> inner_param_bindings;
	const auto& inner_params = inner_matching_ctor->parameter_nodes();
	std::vector<EvalResult> inner_ctor_args;
	inner_ctor_args.push_back(init_arg_result);
	auto bind_result = bind_pre_evaluated_arguments(
		inner_params,
		inner_ctor_args,
		inner_param_bindings,
		"Invalid parameter node in inner constexpr constructor binding",
		true);
	if (!bind_result.success()) {
		return bind_result;
	}

	if (auto member_result = try_evaluate_member_from_constructor_initializers(
		inner_struct_info,
		*inner_matching_ctor,
		inner_param_bindings,
		final_member_name,
		context)) {
		return *member_result;
	}

	return EvalResult::error("Final member '" + std::string(final_member_name) + "' not found in inner struct");
}

// Evaluate array subscript followed by member access (e.g., arr[0].member)
EvalResult Evaluator::evaluate_array_subscript_member_access(
	const ArraySubscriptNode& subscript,
	std::string_view member_name,
	EvaluationContext& context) {
	auto get_array_elements_for_identifier =
		[&context](const IdentifierNode& identifier, bool& found_preferred_static_member) -> std::optional<std::vector<ASTNode>> {
			std::string_view array_name = identifier.name();
			found_preferred_static_member = false;

			if (!context.symbols) {
				return std::nullopt;
			}

			auto extract_elements = [](const std::optional<ASTNode>& initializer) -> std::optional<std::vector<ASTNode>> {
				if (!initializer.has_value() || !initializer->is<InitializerListNode>()) {
					return std::nullopt;
				}

				const InitializerListNode& init_list = initializer->as<InitializerListNode>();
				return init_list.initializers();
			};

			if (auto static_member_result = resolve_current_struct_static_member(
				&identifier,
				context,
				CurrentStructStaticLookupMode::PreferCurrentStruct);
				static_member_result.static_member) {
				found_preferred_static_member = true;
				if (auto elements = extract_elements(static_member_result.static_member->initializer)) {
					return elements;
				}

				StringHandle qualified_handle = StringTable::getOrInternStringHandle(
					StringBuilder().append(static_member_result.owner_struct->getName()).append("::"sv).append(identifier.nameHandle()).commit());
				auto qualified_symbol = context.symbols->lookup(qualified_handle);
				if (qualified_symbol.has_value() && qualified_symbol->is<VariableDeclarationNode>()) {
					const VariableDeclarationNode& qualified_var = qualified_symbol->as<VariableDeclarationNode>();
					if (qualified_var.is_constexpr()) {
						if (auto elements = extract_elements(qualified_var.initializer())) {
							return elements;
						}
					}
				}

				return std::nullopt;
			}

			std::optional<ASTNode> symbol_opt = lookup_identifier_symbol(&identifier, array_name, *context.symbols);
			if (!symbol_opt.has_value()) {
				return std::nullopt;
			}

			const ASTNode& symbol_node = symbol_opt.value();
			if (!symbol_node.is<VariableDeclarationNode>()) {
				return std::nullopt;
			}

			const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
			if (!var_decl.is_constexpr()) {
				return std::nullopt;
			}

			return extract_elements(var_decl.initializer());
		};

	auto extractConstructorCall = [](const ASTNode& element) -> const ConstructorCallNode* {
		if (element.is<ConstructorCallNode>()) {
			return &element.as<ConstructorCallNode>();
		}
		if (!element.is<ExpressionNode>()) {
			return nullptr;
		}

		const ExpressionNode& element_expr = element.as<ExpressionNode>();
		if (std::holds_alternative<ConstructorCallNode>(element_expr)) {
			return &std::get<ConstructorCallNode>(element_expr);
		}
		return nullptr;
	};

	auto evaluateMemberFromCtorCall = [&context, member_name](const ConstructorCallNode& ctor_call) -> EvalResult {
		const ASTNode& type_node = ctor_call.type_node();
		if (!type_node.is<TypeSpecifierNode>()) {
			return EvalResult::error("Invalid struct element type in array subscript member access");
		}

		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
		const StructTypeInfo* struct_info = get_struct_info_from_type(type_spec);
		if (!struct_info) {
			return EvalResult::error("Array element is not a struct in member access");
		}

		const auto& ctor_args = ctor_call.arguments();
		const ConstructorDeclarationNode* matching_ctor = nullptr;
		auto ctor_candidates = struct_info->getConstructorsByParameterCount(ctor_args.size(), false);
		for (const StructMemberFunction* candidate : ctor_candidates) {
			if (!candidate || !candidate->function_decl.is<ConstructorDeclarationNode>()) {
				continue;
			}
			const ConstructorDeclarationNode& candidate_ctor = candidate->function_decl.as<ConstructorDeclarationNode>();
			const auto& params = candidate_ctor.parameter_nodes();
			bool has_static_cast_arg = false;
			bool matches_cast_targets = true;
			for (size_t i = 0; i < params.size() && i < ctor_args.size(); ++i) {
				if (!ctor_args[i].is<ExpressionNode>()) {
					continue;
				}
				const ExpressionNode& arg_expr = ctor_args[i].as<ExpressionNode>();
				if (!std::holds_alternative<StaticCastNode>(arg_expr)) {
					continue;
				}
				has_static_cast_arg = true;
				if (!params[i].is<DeclarationNode>()) {
					matches_cast_targets = false;
					break;
				}

				const StaticCastNode& cast_node = std::get<StaticCastNode>(arg_expr);
				const ASTNode& cast_type_node = cast_node.target_type();
				const DeclarationNode& param_decl = params[i].as<DeclarationNode>();
				const ASTNode& param_type_node = param_decl.type_node();
				if (!cast_type_node.is<TypeSpecifierNode>() || !param_type_node.is<TypeSpecifierNode>()) {
					matches_cast_targets = false;
					break;
				}

				const TypeSpecifierNode& cast_type = cast_type_node.as<TypeSpecifierNode>();
				const TypeSpecifierNode& param_type = param_type_node.as<TypeSpecifierNode>();
				if (cast_type.type() != param_type.type() ||
				    cast_type.type_index() != param_type.type_index() ||
				    cast_type.pointer_depth() != param_type.pointer_depth()) {
					matches_cast_targets = false;
					break;
				}
			}

			if (has_static_cast_arg && matches_cast_targets) {
				matching_ctor = &candidate_ctor;
				break;
			}
		}
		if (!matching_ctor) {
			matching_ctor = find_matching_constructor_by_parameter_count(struct_info, ctor_args.size());
		}
		if (!matching_ctor) {
			return EvalResult::error("No matching constructor found for constexpr array element");
		}

		std::vector<EvalResult> evaluated_ctor_args;
		evaluated_ctor_args.reserve(ctor_args.size());
		for (const auto& ctor_arg : ctor_args) {
			auto arg_result = evaluate(ctor_arg, context);
			if (!arg_result.success()) {
				return arg_result;
			}
			evaluated_ctor_args.push_back(arg_result);
		}

		std::unordered_map<std::string_view, EvalResult> ctor_param_bindings;
		const auto& params = matching_ctor->parameter_nodes();
		auto bind_result = bind_pre_evaluated_arguments(
			params,
			evaluated_ctor_args,
			ctor_param_bindings,
			"Invalid parameter node in array element constructor binding",
			true);
		if (!bind_result.success()) {
			return bind_result;
		}

		if (auto member_result = try_evaluate_member_from_constructor_initializers(
			struct_info,
			*matching_ctor,
			ctor_param_bindings,
			member_name,
			context)) {
			return *member_result;
		}

		return EvalResult::error("Member '" + std::string(member_name) + "' not found in array element");
	};

	auto index_result = evaluate(subscript.index_expr(), context);
	if (!index_result.success()) {
		return index_result;
	}

	long long evaluated_index = index_result.as_int();
	if (evaluated_index < 0) {
		return EvalResult::error("Negative array index in constant expression");
	}
	if (static_cast<unsigned long long>(evaluated_index) > std::numeric_limits<size_t>::max()) {
		return EvalResult::error("Array index too large in constant expression");
	}

	const ASTNode& array_expr = subscript.array_expr();
	const IdentifierNode* array_identifier = nullptr;
	if (array_expr.is<ExpressionNode>()) {
		const ExpressionNode& expr = array_expr.as<ExpressionNode>();
		if (std::holds_alternative<IdentifierNode>(expr)) {
			array_identifier = &std::get<IdentifierNode>(expr);
		}
	} else if (array_expr.is<IdentifierNode>()) {
		array_identifier = &array_expr.as<IdentifierNode>();
	}
	if (!array_identifier) {
		return EvalResult::error("Array subscript followed by member access requires identifier base");
	}

	bool found_preferred_static_member = false;
	auto elements = get_array_elements_for_identifier(*array_identifier, found_preferred_static_member);
	if (!elements) {
		if (found_preferred_static_member) {
			return EvalResult::error("Array subscript member access requires a constexpr initializer list on the preferred static member array");
		}
		return EvalResult::error("Array subscript member access requires constexpr array initializer list");
	}

	size_t element_index = static_cast<size_t>(evaluated_index);
	if (element_index >= elements->size()) {
		return EvalResult::error("Array index " + std::to_string(element_index) + " out of bounds (size " + std::to_string(elements->size()) + ")");
	}

	const ASTNode& array_element = (*elements)[element_index];
	const ConstructorCallNode* ctor_call_ptr = extractConstructorCall(array_element);
	if (!ctor_call_ptr) {
		return EvalResult::error("Array element in member access is not a constructor call");
	}

	return evaluateMemberFromCtorCall(*ctor_call_ptr);
}

EvalResult Evaluator::evaluate_static_member_initializer_or_default(
	const StructStaticMember& static_member,
	EvaluationContext& context) {
	if (static_member.initializer.has_value()) {
		return evaluate(static_member.initializer.value(), context);
	}

	if (static_member.type == Type::Bool) {
		return EvalResult::from_bool(false);
	}
	return EvalResult::from_int(0);
}

// Helper function to look up and evaluate static member from struct info
EvalResult Evaluator::evaluate_static_member_from_struct(
	const StructTypeInfo* struct_info,
	const TypeInfo& type_info,
	StringHandle member_name_handle,
	std::string_view member_name,
	EvaluationContext& context) {
	
	// Look up the static member in the struct
	// Search for a static member variable with the given name
	for (const auto& static_member : struct_info->static_members) {
		if (static_member.getName() == member_name_handle) {
			// Found the static member - check if it has an initializer
			if (static_member.initializer.has_value()) {
				// Evaluate the initializer directly
				return evaluate(*static_member.initializer, context);
			}
			
			// If no inline initializer, try to find the definition in the symbol table
			// Build qualified member name using StringBuilder
			StringBuilder qualified_name_builder;
			qualified_name_builder.append(StringTable::getStringView(type_info.name_));
			qualified_name_builder.append("::");
			qualified_name_builder.append(member_name);
			std::string_view qualified_member_name = qualified_name_builder.commit();
			
			auto member_symbol = context.symbols->lookup(qualified_member_name);
			if (member_symbol.has_value()) {
				const ASTNode& member_node = member_symbol.value();
				if (member_node.is<VariableDeclarationNode>()) {
					const VariableDeclarationNode& var_decl = member_node.as<VariableDeclarationNode>();
					if (var_decl.is_constexpr() && var_decl.initializer().has_value()) {
						return evaluate(*var_decl.initializer(), context);
					}
				}
			}
			
			return EvalResult::error("Static member '" + std::string(member_name) + "' found but has no constexpr initializer");
		}
	}
	
	return EvalResult::error("Member '" + std::string(member_name) + "' not found in return type");
}

// Evaluate function call followed by member access (e.g., get_struct().member)
// This is used for accessing static members of the return type
EvalResult Evaluator::evaluate_function_call_member_access(
	const FunctionCallNode& func_call,
	std::string_view member_name,
	EvaluationContext& context) {
	
	// Get the function declaration to determine return type
	const DeclarationNode& func_decl_node = func_call.function_declaration();
	// Convert member_name to StringHandle once for efficient comparison
	StringHandle member_name_handle = StringTable::getOrInternStringHandle(member_name);
	
	// Prefer the declaration already attached to the FunctionCallNode.
	// This preserves member-vs-global resolution decisions that were already made by the parser.
	const ASTNode& type_node = func_decl_node.type_node();
	if (!type_node.is<TypeSpecifierNode>()) {
		return EvalResult::error("Function return type is not a TypeSpecifierNode");
	}
	
	const TypeSpecifierNode& return_type = type_node.as<TypeSpecifierNode>();
	
	// Get the type name - this should be a struct/class type
	if (return_type.type() != Type::UserDefined && return_type.type() != Type::Struct) {
		return EvalResult::error("Function return type is not a struct - cannot access member");
	}
	
	// Get the struct type name
	TypeIndex type_index = return_type.type_index();
	if (type_index >= gTypeInfo.size()) {
		return EvalResult::error("Invalid type index for function return type");
	}
	
	const TypeInfo& type_info = gTypeInfo[type_index];
	const StructTypeInfo* struct_info = type_info.getStructInfo();
	if (!struct_info) {
		return EvalResult::error("Return type is not a struct");
	}
	
	// Use the helper function to look up and evaluate the static member
	return evaluate_static_member_from_struct(struct_info, type_info, member_name_handle, member_name, context);
}

// Evaluate constexpr member function call (e.g., p.sum() in constexpr context)
EvalResult Evaluator::evaluate_member_function_call(const MemberFunctionCallNode& member_func_call, EvaluationContext& context) {
	// Check recursion depth
	if (context.current_depth >= context.max_recursion_depth) {
		return EvalResult::error("Constexpr recursion depth limit exceeded in member function call");
	}
	
	// Get the object being called on
	const ASTNode& object_expr = member_func_call.object();
	
	// Get the function name from the placeholder FunctionDeclarationNode
	const FunctionDeclarationNode& placeholder_func = member_func_call.function_declaration();
	std::string_view func_name = placeholder_func.decl_node().identifier_token().value();
	
	// For lambda calls (operator()), we need special handling
	const bool is_operator_call = (func_name == "operator()");

	if (is_operator_call) {
		auto extract_lambda_from_object_expr = [&]() -> const LambdaExpressionNode* {
			if (object_expr.is<LambdaExpressionNode>()) {
				return &object_expr.as<LambdaExpressionNode>();
			}
			if (object_expr.is<ExpressionNode>()) {
				const ExpressionNode& expr_node = object_expr.as<ExpressionNode>();
				if (std::holds_alternative<LambdaExpressionNode>(expr_node)) {
					return &std::get<LambdaExpressionNode>(expr_node);
				}
			}
			return nullptr;
		};

		if (const LambdaExpressionNode* object_lambda = extract_lambda_from_object_expr()) {
			return evaluate_lambda_call(*object_lambda, member_func_call.arguments(), context);
		}
	}

	auto try_evaluate_current_struct_static_member = [&]() -> std::optional<EvalResult> {
		StringHandle fn_handle = StringTable::getOrInternStringHandle(func_name);
		auto current_match = find_current_struct_member_function_candidate(
			fn_handle,
			member_func_call.arguments().size(),
			context,
			MemberFunctionLookupMode::ConstexprEvaluable,
			true,
			false);
		const FunctionDeclarationNode* matched_function = current_match.function;

		if (!matched_function) {
			return std::nullopt;
		}

		std::unordered_map<std::string_view, EvalResult> empty_b;
		return evaluate_function_call_with_template_context(
			*matched_function,
			member_func_call.arguments(),
			empty_b,
			context,
			nullptr,
			FunctionCallTemplateBindingLoadMode::ForceCurrentStructIfAvailable);
	};
	
	// First, we need to get the struct type from the object to look up the actual function
	std::string_view var_name;
	const IdentifierNode* object_identifier = nullptr;
	
	if (object_expr.is<ExpressionNode>()) {
		const ExpressionNode& expr_node = object_expr.as<ExpressionNode>();
		if (!std::holds_alternative<IdentifierNode>(expr_node)) {
			return EvalResult::error("Complex object expressions not yet supported in constexpr member function calls");
		}
		object_identifier = &std::get<IdentifierNode>(expr_node);
		var_name = object_identifier->name();
	} else if (object_expr.is<IdentifierNode>()) {
		object_identifier = &object_expr.as<IdentifierNode>();
		var_name = object_identifier->name();
	} else {
		return EvalResult::error("Complex object expressions not yet supported in constexpr member function calls");
	}

	if (placeholder_func.is_static() && object_identifier && object_identifier->name() == "this") {
		if (auto static_member_result = try_evaluate_current_struct_static_member()) {
			return *static_member_result;
		}
	}

	const VariableDeclarationNode* var_decl = nullptr;
	ResolvedConstexprObject resolved_object;
	if (auto resolve_error = resolve_constexpr_object_source(
		object_identifier,
		var_name,
		context,
		"member function call",
		resolved_object)) {
		return *resolve_error;
	}

	var_decl = resolved_object.var_decl;
	if (var_decl && !var_decl->is_constexpr()) {
		return EvalResult::error("Variable in member function call must be constexpr: " + std::string(var_name));
	}

	const std::optional<ASTNode>* initializer = resolved_object.initializer;
	TypeIndex declared_type_index = resolved_object.declared_type_index;
	if (!initializer->has_value()) {
		return EvalResult::error("Constexpr variable has no initializer: " + std::string(var_name));
	}

	if (!var_decl) {
		if (auto static_member_result = try_evaluate_current_struct_static_member()) {
			return *static_member_result;
		}
	}

	// Check if this is a lambda call (operator() on a lambda object)
	if (is_operator_call) {
		const LambdaExpressionNode* lambda = extract_lambda_from_initializer(*initializer);
		if (lambda) {
			return evaluate_lambda_call(*lambda, member_func_call.arguments(), context);
		}
		// Brace-initialized or ConstructorCallNode-initialized callable: delegate to evaluate_callable_object
		if (initializer->has_value() && ((*initializer)->is<InitializerListNode>() || extract_constructor_call(*initializer))) {
			if (!var_decl) {
				return EvalResult::error("Callable object is not a variable");
			}
			return evaluate_callable_object(*var_decl, member_func_call.arguments(), context);
		}
	}
	
	const ConstructorCallNode* ctor_call_ptr = extract_constructor_call(*initializer);
	if (!ctor_call_ptr && !(*initializer)->is<InitializerListNode>()) {
		return EvalResult::error("Member function calls require struct/class objects");
	}
	
	// Resolve the struct type info. For ConstructorCallNode initializers we get it from
	// the constructor's type node; for brace-initialized (InitializerListNode) objects we
	// resolve it from the variable's declared type instead.
	const StructTypeInfo* struct_info = nullptr;
	TypeIndex type_index{0};
	
	if (ctor_call_ptr) {
		const ConstructorCallNode& ctor_call = *ctor_call_ptr;
		const ASTNode& type_node = ctor_call.type_node();
		if (!type_node.is<TypeSpecifierNode>()) {
			return EvalResult::error("Constructor call without valid type specifier");
		}
		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
		if (type_spec.type() != Type::Struct && type_spec.type() != Type::UserDefined) {
			return EvalResult::error("Member function call requires a struct type");
		}
		type_index = type_spec.type_index();
		if (type_index < gTypeInfo.size()) {
			struct_info = gTypeInfo[type_index].getStructInfo();
		}
		if (!struct_info && declared_type_index != TypeIndex{0} && declared_type_index < gTypeInfo.size()) {
			type_index = declared_type_index;
			struct_info = gTypeInfo[type_index].getStructInfo();
		}
	} else {
		// Brace-initialized object: resolve type from the declared object type.
		if (declared_type_index == TypeIndex{0} || declared_type_index >= gTypeInfo.size()) {
			return EvalResult::error("Brace-initialized object has invalid type in member function call");
		}
		type_index = declared_type_index;
		struct_info = gTypeInfo[type_index].getStructInfo();
	}
	
	if (!struct_info) {
		return EvalResult::error("Type is not a struct in member function call");
	}
	
	// Look up the actual member function in the struct's type info
	const auto& arguments = member_func_call.arguments();
	StringHandle func_name_handle = StringTable::getOrInternStringHandle(func_name);
	auto member_function_match = find_member_function_candidate(
		struct_info,
		func_name_handle,
		arguments.size(),
		context,
		MemberFunctionLookupMode::LookupOnly,
		false,
		true);
	if (member_function_match.ambiguous) {
		return EvalResult::error("Ambiguous member function overload in constant expression");
	}
	const FunctionDeclarationNode* actual_func = member_function_match.function;
	
	if (!actual_func) {
		return EvalResult::error("Member function not found: " + std::string(func_name));
	}
	
	// Check if it's a constexpr function
	if (!actual_func->is_constexpr()) {
		return EvalResult::error("Member function must be constexpr: " + std::string(func_name));
	}
	
	// Get the function body
	const auto& definition = actual_func->get_definition();
	if (!definition.has_value()) {
		return EvalResult::error("Constexpr member function has no body: " + std::string(func_name));
	}
	
	// Extract member values from the object for 'this' access
	std::unordered_map<std::string_view, EvalResult> member_bindings;
	
	auto member_extraction_result = extract_object_members(object_expr, member_bindings, context);
	if (!member_extraction_result.success()) {
		return member_extraction_result;
	}
	
	// Evaluate function arguments and add to bindings
	const auto& parameters = actual_func->parameter_nodes();
	auto bind_result = bind_evaluated_arguments(
		parameters,
		arguments,
		member_bindings,
		context,
		"Invalid parameter node in constexpr member function");
	if (!bind_result.success()) {
		return bind_result;
	}

	auto saved_template_param_names = context.template_param_names;
	auto saved_template_args = context.template_args;
	auto restore_template_bindings = [&]() {
		context.template_param_names = std::move(saved_template_param_names);
		context.template_args = std::move(saved_template_args);
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
	} else {
		load_template_bindings_from_type(&gTypeInfo[type_index], context);
	}
		auto saved_struct_info = context.struct_info;
		context.struct_info = struct_info;
	
	// Increase recursion depth
	context.current_depth++;
	
	// Evaluate the function body
	auto result = evaluate_block_with_bindings(
		definition.value(),
		member_bindings,
		context,
		"Member function body is not a block",
		"Constexpr member function did not return a value");
	context.current_depth--;
		context.struct_info = saved_struct_info;
	restore_template_bindings();
	return result;
}

// Shared helper: bind struct members from an InitializerListNode (aggregate init)
// and apply default member initializers for any members not covered by the list.
EvalResult Evaluator::bind_members_from_initializer_list(
	const StructTypeInfo* struct_info,
	const InitializerListNode& init_list,
	std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context) {
	// Bind members covered by the initializer list.
	for (size_t mi = 0; mi < struct_info->members.size() && mi < init_list.size(); ++mi) {
		std::string_view mname;
		if (init_list.is_designated(mi)) {
			// Designated initializer: use the member name from the designator
			mname = StringTable::getStringView(init_list.member_name(mi));
		} else {
			// Positional initializer: use the struct member at this index
			mname = StringTable::getStringView(struct_info->members[mi].getName());
		}
		auto val = evaluate(init_list.initializers()[mi], context);
		if (!val.success()) return val;
		bindings[mname] = val;
	}
	// Apply default member initializers for remaining members.
	for (size_t mi = 0; mi < struct_info->members.size(); ++mi) {
		const auto& member = struct_info->members[mi];
		std::string_view mname = StringTable::getStringView(member.getName());
		if (bindings.find(mname) == bindings.end() && member.default_initializer.has_value()) {
			auto default_result = evaluate(member.default_initializer.value(), context);
			if (!default_result.success()) return default_result;
			bindings[mname] = default_result;
		}
	}
	return EvalResult::from_bool(true);
}

EvalResult Evaluator::bind_members_from_constructor_initializers(
	const StructTypeInfo* struct_info,
	const ConstructorDeclarationNode& ctor_decl,
	std::unordered_map<std::string_view, EvalResult>& ctor_param_bindings,
	std::unordered_map<std::string_view, EvalResult>& member_bindings,
	EvaluationContext& context,
	bool ignore_default_initializer_errors) {
	for (const auto& mem_init : ctor_decl.member_initializers()) {
		auto member_result = evaluate_expression_with_bindings(mem_init.initializer_expr, ctor_param_bindings, context);
		if (!member_result.success()) {
			return member_result;
		}
		member_bindings[mem_init.member_name] = member_result;
	}

	for (const auto& member : struct_info->members) {
		std::string_view member_name = StringTable::getStringView(member.getName());
		if (member_bindings.find(member_name) != member_bindings.end() || !member.default_initializer.has_value()) {
			continue;
		}

		auto default_result = evaluate(member.default_initializer.value(), context);
		if (!default_result.success()) {
			if (ignore_default_initializer_errors) {
				continue;
			}
			return default_result;
		}

		member_bindings[member_name] = default_result;
	}

	return EvalResult::from_bool(true);
}

std::optional<EvalResult> Evaluator::try_evaluate_member_from_constructor_initializers(
	const StructTypeInfo* struct_info,
	const ConstructorDeclarationNode& ctor_decl,
	std::unordered_map<std::string_view, EvalResult>& ctor_param_bindings,
	std::string_view member_name,
	EvaluationContext& context) {
	for (const auto& mem_init : ctor_decl.member_initializers()) {
		if (mem_init.member_name == member_name) {
			return evaluate_expression_with_bindings(mem_init.initializer_expr, ctor_param_bindings, context);
		}
	}

	StringHandle member_name_handle = StringTable::getOrInternStringHandle(member_name);
	for (const auto& member : struct_info->members) {
		if (member.getName() == member_name_handle && member.default_initializer.has_value()) {
			return evaluate(member.default_initializer.value(), context);
		}
	}

	return std::nullopt;
}

// Helper to extract member values from a constexpr object
EvalResult Evaluator::extract_object_members(
	const ASTNode& object_expr,
	std::unordered_map<std::string_view, EvalResult>& member_bindings,
	EvaluationContext& context) {
	
	// Get the object variable name
	std::string_view var_name;
	const IdentifierNode* object_identifier = nullptr;
	
	if (object_expr.is<ExpressionNode>()) {
		const ExpressionNode& expr_node = object_expr.as<ExpressionNode>();
		if (!std::holds_alternative<IdentifierNode>(expr_node)) {
			return EvalResult::error("Complex object expressions not yet supported in constexpr member function calls");
		}
		object_identifier = &std::get<IdentifierNode>(expr_node);
		var_name = object_identifier->name();
	} else if (object_expr.is<IdentifierNode>()) {
		object_identifier = &object_expr.as<IdentifierNode>();
		var_name = object_identifier->name();
	} else {
		return EvalResult::error("Complex object expressions not yet supported in constexpr member function calls");
	}

	const VariableDeclarationNode* var_decl = nullptr;
	ResolvedConstexprObject resolved_object;
	if (auto resolve_error = resolve_constexpr_object_source(
		object_identifier,
		var_name,
		context,
		"member function call",
		resolved_object)) {
		return *resolve_error;
	}

	var_decl = resolved_object.var_decl;
	if (var_decl && !var_decl->is_constexpr()) {
		return EvalResult::error("Variable in member function call must be constexpr: " + std::string(var_name));
	}

	const std::optional<ASTNode>* initializer = resolved_object.initializer;
	TypeIndex declared_type_index = resolved_object.declared_type_index;
	if (!initializer->has_value()) {
		return EvalResult::error("Constexpr variable has no initializer: " + std::string(var_name));
	}
	
	// Handle brace-initialized objects (aggregate init): extract member values by position.
	if ((*initializer)->is<InitializerListNode>()) {
		if (declared_type_index == TypeIndex{0} || declared_type_index >= gTypeInfo.size()) {
			return EvalResult::error("Brace-initialized object has invalid type");
		}
		const StructTypeInfo* agg_struct_info = gTypeInfo[declared_type_index].getStructInfo();
		if (!agg_struct_info)
			return EvalResult::error("Brace-initialized object is not a struct");
		const InitializerListNode& init_list = (*initializer)->as<InitializerListNode>();
		return bind_members_from_initializer_list(agg_struct_info, init_list, member_bindings, context);
	}

	const ConstructorCallNode* ctor_call_ptr = extract_constructor_call(*initializer);
	if (!ctor_call_ptr) {
		return EvalResult::error("Member function calls require struct/class objects");
	}
	
	const ConstructorCallNode& ctor_call = *ctor_call_ptr;
	
	// Get the struct type info
	const ASTNode& type_node = ctor_call.type_node();
	if (!type_node.is<TypeSpecifierNode>()) {
		return EvalResult::error("Constructor call without valid type specifier");
	}
	
	const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
	
	if (type_spec.type() != Type::Struct && type_spec.type() != Type::UserDefined) {
		return EvalResult::error("Member function call requires a struct type");
	}
	
	TypeIndex type_index = type_spec.type_index();
	const TypeInfo* struct_type_info = nullptr;
	const StructTypeInfo* struct_info = nullptr;
	if (type_index < gTypeInfo.size()) {
		struct_type_info = &gTypeInfo[type_index];
		struct_info = struct_type_info->getStructInfo();
	}
	if (!struct_info && declared_type_index != TypeIndex{0} && declared_type_index < gTypeInfo.size()) {
		type_index = declared_type_index;
		struct_type_info = &gTypeInfo[type_index];
		struct_info = struct_type_info->getStructInfo();
	}
	if (!struct_info) {
		return EvalResult::error("Type is not a struct in member function call");
	}
	
	const auto& ctor_args = ctor_call.arguments();
	
	// Find the matching constructor
	const ConstructorDeclarationNode* matching_ctor = find_matching_constructor_by_parameter_count(struct_info, ctor_args.size());
	
	if (!matching_ctor) {
		return EvalResult::error("No matching constructor found for constexpr object");
	}
	
	// Build parameter bindings for the constructor
	std::unordered_map<std::string_view, EvalResult> ctor_param_bindings;
	const auto& params = matching_ctor->parameter_nodes();
	auto bind_result = bind_evaluated_arguments(
		params,
		ctor_args,
		ctor_param_bindings,
		context,
		"Invalid parameter node in constexpr constructor member extraction",
		nullptr,
		true);
	if (!bind_result.success()) {
		return bind_result;
	}
	
	auto member_bind_result = bind_members_from_constructor_initializers(
		struct_info,
		*matching_ctor,
		ctor_param_bindings,
		member_bindings,
		context,
		true);
	if (!member_bind_result.success()) {
		return member_bind_result;
	}
	
	return EvalResult::from_bool(true);  // Success
}

// Evaluate array subscript (e.g., arr[0] or obj.data[1])
EvalResult Evaluator::evaluate_array_subscript(const ArraySubscriptNode& subscript, EvaluationContext& context) {
	// First, evaluate the index expression to get the constant index
	auto index_result = evaluate(subscript.index_expr(), context);
	if (!index_result.success()) {
		return index_result;
	}
	
	long long index = index_result.as_int();
	if (index < 0) {
		return EvalResult::error("Negative array index in constant expression");
	}
	
	// Get the array expression - this could be:
	// 1. A member access (e.g., obj.data)
	// 2. An identifier (e.g., arr)
	const ASTNode& array_expr = subscript.array_expr();
	
	// Check if it's a member access (e.g., obj.data[0])
	if (array_expr.is<ExpressionNode>()) {
		const ExpressionNode& expr = array_expr.as<ExpressionNode>();
		if (std::holds_alternative<MemberAccessNode>(expr)) {
			return evaluate_member_array_subscript(std::get<MemberAccessNode>(expr), static_cast<size_t>(index), context);
		}
		if (std::holds_alternative<IdentifierNode>(expr)) {
			return evaluate_variable_array_subscript(std::get<IdentifierNode>(expr), static_cast<size_t>(index), context);
		}
	}
	if (array_expr.is<IdentifierNode>()) {
		return evaluate_variable_array_subscript(array_expr.as<IdentifierNode>(), static_cast<size_t>(index), context);
	}
	
	return EvalResult::error("Array subscript on unsupported expression type");
}

// Evaluate array subscript on a member (e.g., obj.data[0])
EvalResult Evaluator::evaluate_member_array_subscript(
	const MemberAccessNode& member_access,
	size_t index,
	EvaluationContext& context) {
	
	const ASTNode& object_expr = member_access.object();
	std::string_view member_name = member_access.member_name();
	std::string_view var_name;
	const IdentifierNode* object_identifier = nullptr;

	if (object_expr.is<ExpressionNode>()) {
		const ExpressionNode& expr_node = object_expr.as<ExpressionNode>();
		if (!std::holds_alternative<IdentifierNode>(expr_node)) {
			return EvalResult::error("Complex expressions in array member access not supported");
		}
		object_identifier = &std::get<IdentifierNode>(expr_node);
		var_name = object_identifier->name();
	} else if (object_expr.is<IdentifierNode>()) {
		object_identifier = &object_expr.as<IdentifierNode>();
		var_name = object_identifier->name();
	} else {
		return EvalResult::error("Invalid object expression in array member access");
	}

	auto evaluate_array_member_element_from_initializer =
		[&](const std::optional<ASTNode>& initializer_opt, TypeIndex declared_type_index) -> EvalResult {
			ResolvedConstexprMemberSource resolved_member;
			if (auto resolve_error = resolve_constexpr_member_source_from_initializer(
				initializer_opt,
				declared_type_index,
				member_name,
				"array subscript",
				context,
				resolved_member)) {
				return *resolve_error;
			}

			if (!resolved_member.initializer.has_value()) {
				return EvalResult::error("Internal error: unresolved array member source");
			}

			if (!resolved_member.initializer->is<InitializerListNode>()) {
				return EvalResult::error("Array member is not initialized with an array initializer");
			}

			const InitializerListNode& init_list = resolved_member.initializer->as<InitializerListNode>();
			const auto& elements = init_list.initializers();
			if (index >= elements.size()) {
				return EvalResult::error("Array index " + std::to_string(index) + " out of bounds (size " + std::to_string(elements.size()) + ")");
			}

			if (!resolved_member.evaluation_bindings.empty()) {
				return evaluate_expression_with_bindings(
					elements[index],
					resolved_member.evaluation_bindings,
					context);
			}

			return evaluate(elements[index], context);
		};

	ResolvedConstexprObject resolved_object;
	if (auto resolve_error = resolve_constexpr_object_source(
		object_identifier,
		var_name,
		context,
		"array subscript",
		resolved_object)) {
		return *resolve_error;
	}

	if (resolved_object.var_decl && !resolved_object.var_decl->is_constexpr()) {
		return EvalResult::error("Variable in array subscript must be constexpr");
	}

	const std::optional<ASTNode>* initializer = resolved_object.initializer;
	TypeIndex declared_type_index = resolved_object.declared_type_index;
	return evaluate_array_member_element_from_initializer(*initializer, declared_type_index);
}

// Evaluate array subscript on a variable (e.g., arr[0] where arr is constexpr)
EvalResult Evaluator::evaluate_variable_array_subscript(
	const IdentifierNode& identifier,
	size_t index,
	EvaluationContext& context) {
	std::string_view var_name = identifier.name();
	auto evaluate_array_initializer = [&](const std::optional<ASTNode>& initializer_opt) -> std::optional<EvalResult> {
		if (!initializer_opt.has_value() || !initializer_opt->is<InitializerListNode>()) {
			return std::nullopt;
		}

		const InitializerListNode& init_list = initializer_opt->as<InitializerListNode>();
		const auto& elements = init_list.initializers();
		if (index >= elements.size()) {
			return EvalResult::error("Array index " + std::to_string(index) + " out of bounds (size " + std::to_string(elements.size()) + ")");
		}
		return evaluate(elements[index], context);
	};
	
	if (!context.symbols) {
		return EvalResult::error("Cannot evaluate array subscript: no symbol table provided");
	}

	if (auto static_member_result = resolve_current_struct_static_member(
		&identifier,
		context,
		CurrentStructStaticLookupMode::PreferCurrentStruct);
		static_member_result.static_member) {
		if (auto static_result = evaluate_array_initializer(static_member_result.static_member->initializer)) {
			return *static_result;
		}

		StringHandle qualified_handle = StringTable::getOrInternStringHandle(
			StringBuilder().append(static_member_result.owner_struct->getName()).append("::"sv).append(identifier.nameHandle()).commit());
		auto qualified_symbol = context.symbols->lookup(qualified_handle);
		if (qualified_symbol.has_value() && qualified_symbol->is<VariableDeclarationNode>()) {
			const VariableDeclarationNode& qualified_var = qualified_symbol->as<VariableDeclarationNode>();
			if (!qualified_var.is_constexpr()) {
				return EvalResult::error("Static member array in array subscript must be constexpr");
			}
			if (auto qualified_result = evaluate_array_initializer(qualified_var.initializer())) {
				return *qualified_result;
			}
		}

		return EvalResult::error("Static member array has no usable initializer in array subscript: " + std::string(var_name));
	}
	
	std::optional<ASTNode> symbol_opt = lookup_identifier_symbol(&identifier, var_name, *context.symbols);
	if (!symbol_opt.has_value()) {
		return EvalResult::error("Undefined variable in array subscript: " + std::string(var_name));
	}
	
	const ASTNode& symbol_node = symbol_opt.value();
	if (!symbol_node.is<VariableDeclarationNode>()) {
		return EvalResult::error("Identifier in array subscript is not a variable");
	}
	
	const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
	if (!var_decl.is_constexpr()) {
		return EvalResult::error("Variable in array subscript must be constexpr");
	}
	
	const auto& initializer = var_decl.initializer();
	if (!initializer.has_value()) {
		return EvalResult::error("Constexpr array has no initializer");
	}
	
	// The initializer should be an InitializerListNode for arrays
	if (initializer->is<InitializerListNode>()) {
		const InitializerListNode& init_list = initializer->as<InitializerListNode>();
		const auto& elements = init_list.initializers();
		
		if (index >= elements.size()) {
			return EvalResult::error("Array index " + std::to_string(index) + " out of bounds (size " + std::to_string(elements.size()) + ")");
		}
		
		return evaluate(elements[index], context);
	}
	
	return EvalResult::error("Array variable is not initialized with an array initializer");
}

// Helper functions for branchless type checking
bool Evaluator::isArithmeticType(Type type) {
	// Branchless: arithmetic types are Bool(1) through LongDouble(14)
	return (static_cast<int_fast16_t>(type) >= static_cast<int_fast16_t>(Type::Bool)) &
	       (static_cast<int_fast16_t>(type) <= static_cast<int_fast16_t>(Type::LongDouble));
}

bool Evaluator::isFundamentalType(Type type) {
	// Branchless: fundamental types are Void(0), Nullptr(28), or arithmetic types
	return (type == Type::Void) | (type == Type::Nullptr) | isArithmeticType(type);
}

// Evaluate type trait expressions (e.g., __is_void(int), __is_constant_evaluated())
EvalResult Evaluator::evaluate_type_trait(const TypeTraitExprNode& trait_expr) {
	// Handle __is_constant_evaluated() specially - it returns true during constexpr evaluation
	if (trait_expr.kind() == TypeTraitKind::IsConstantEvaluated) {
		// When evaluated in constexpr context, this always returns true
		return EvalResult::from_bool(true);
	}

	// For other type traits, we need to evaluate them based on the type
	// Most type traits can be evaluated at compile time
	if (!trait_expr.has_type()) {
		return EvalResult::error("Type trait requires a type argument");
	}

	const ASTNode& type_node = trait_expr.type_node();
	if (!type_node.is<TypeSpecifierNode>()) {
		return EvalResult::error("Type trait argument must be a type");
	}

	const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
	Type type = type_spec.type();
	bool is_reference = type_spec.is_reference();
	bool is_rvalue_reference = type_spec.is_rvalue_reference();
	size_t pointer_depth = type_spec.pointer_depth();

	bool result = false;

	// Evaluate the type trait based on its kind
	switch (trait_expr.kind()) {
		case TypeTraitKind::IsVoid:
			result = (type == Type::Void && !is_reference && pointer_depth == 0);
			break;

		case TypeTraitKind::IsIntegral:
			result = (type == Type::Bool ||
			         type == Type::Char ||
			         type == Type::Short || type == Type::Int || type == Type::Long || type == Type::LongLong ||
			         type == Type::UnsignedChar || type == Type::UnsignedShort || type == Type::UnsignedInt ||
			         type == Type::UnsignedLong || type == Type::UnsignedLongLong)
			         && !is_reference && pointer_depth == 0;
			break;

		case TypeTraitKind::IsFloatingPoint:
			result = (type == Type::Float || type == Type::Double || type == Type::LongDouble)
			         && !is_reference && pointer_depth == 0;
			break;

		case TypeTraitKind::IsPointer:
			result = (pointer_depth > 0) && !is_reference;
			break;

		case TypeTraitKind::IsLvalueReference:
			result = is_reference && !is_rvalue_reference;
			break;

		case TypeTraitKind::IsRvalueReference:
			result = is_rvalue_reference;
			break;

		case TypeTraitKind::IsArray:
			result = type_spec.is_array() && !is_reference && pointer_depth == 0;
			break;

		case TypeTraitKind::IsReference:
			result = is_reference | is_rvalue_reference;
			break;

		case TypeTraitKind::IsArithmetic:
			result = isArithmeticType(type) & !is_reference & (pointer_depth == 0);
			break;

		case TypeTraitKind::IsFundamental:
			result = isFundamentalType(type) & !is_reference & (pointer_depth == 0);
			break;

		case TypeTraitKind::IsObject:
			result = (type != Type::Function) & (type != Type::Void) & !is_reference & !is_rvalue_reference;
			break;

		case TypeTraitKind::IsScalar:
			result = (isArithmeticType(type) ||
			          type == Type::Enum || type == Type::Nullptr ||
			          type == Type::MemberObjectPointer || type == Type::MemberFunctionPointer ||
			          pointer_depth > 0)
			          && !is_reference;
			break;

		case TypeTraitKind::IsCompound:
			result = !(isFundamentalType(type) & !is_reference & (pointer_depth == 0));
			break;

		case TypeTraitKind::IsConst:
			result = type_spec.is_const();
			break;

		case TypeTraitKind::IsVolatile:
			result = type_spec.is_volatile();
			break;

		case TypeTraitKind::IsSigned:
			result = ((type == Type::Char || type == Type::Short || type == Type::Int ||
			          type == Type::Long || type == Type::LongLong)
			          && !is_reference && pointer_depth == 0);
			break;

		case TypeTraitKind::IsUnsigned:
			result = ((type == Type::Bool || type == Type::UnsignedChar || type == Type::UnsignedShort ||
			          type == Type::UnsignedInt || type == Type::UnsignedLong || type == Type::UnsignedLongLong)
			          && !is_reference && pointer_depth == 0);
			break;

		case TypeTraitKind::IsBoundedArray:
			result = type_spec.is_array() & int(type_spec.array_size() > 0) & !is_reference & (pointer_depth == 0);
			break;

		case TypeTraitKind::IsUnboundedArray:
			result = type_spec.is_array() & int(type_spec.array_size() == 0) & !is_reference & (pointer_depth == 0);
			break;

		case TypeTraitKind::IsAggregate:
			// Arrays are aggregates
			result = type_spec.is_array() & !is_reference & (pointer_depth == 0);
			// For struct types, we need runtime type info, so fall through to default
			break;

		case TypeTraitKind::IsCompleteOrUnbounded:
			// __is_complete_or_unbounded evaluates to true if either:
			// 1. T is a complete type, or
			// 2. T is an unbounded array type (e.g. int[])
			// Returns false for: void, incomplete class types, bounded arrays with incomplete elements
			
			// Check for void - always incomplete
			if (type == Type::Void && pointer_depth == 0 && !is_reference) {
				return EvalResult::from_bool(false);
			}
			
			// Check for unbounded array - always returns true
			if (type_spec.is_array() && type_spec.array_size() == 0) {
				return EvalResult::from_bool(true);
			}
			
			// Check for incomplete class/struct types
			// A type is incomplete if it's a struct/class with no StructTypeInfo
			if ((type == Type::Struct || type == Type::UserDefined) && 
			    pointer_depth == 0 && !is_reference) {
				TypeIndex type_idx = type_spec.type_index();
				if (type_idx != TypeIndex{0}) {
					const TypeInfo& type_info = gTypeInfo[type_idx];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					// If no struct_info, the type is incomplete
					if (!struct_info) {
						return EvalResult::from_bool(false);
					}
				}
			}
			
			// All other types are considered complete
			return EvalResult::from_bool(true);

		// Add more type traits as needed
		// For now, other type traits return false during constexpr evaluation
		default:
			result = false;
			break;
	}

	return EvalResult::from_bool(result);
}

} // namespace ConstExpr
