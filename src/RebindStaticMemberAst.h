#pragma once

#include "CallNodeHelpers.h"
#include "AstNodeTypes_Stmt.h"
#include "AstNodeTypes_Template.h"
#include <type_traits>

namespace RebindStaticMemberAst {

inline std::pair<const FunctionDeclarationNode*, const StructTypeInfo*> findStaticMemberFunction(
	const StructTypeInfo* struct_info,
	StringHandle function_name) {
	if (!struct_info) {
		return {nullptr, nullptr};
	}

	auto find_in_struct = [function_name](const StructTypeInfo* candidate_struct)
		-> std::pair<const FunctionDeclarationNode*, const StructTypeInfo*> {
		if (!candidate_struct) {
			return {nullptr, nullptr};
		}

		const FunctionDeclarationNode* function_decl_without_definition = nullptr;
		for (const auto& member_func : candidate_struct->member_functions) {
			if (member_func.getName() != function_name || !member_func.function_decl.is<FunctionDeclarationNode>()) {
				continue;
			}

			const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
			if (func_decl.is_static()) {
				if (func_decl.get_definition().has_value()) {
					return {&func_decl, candidate_struct};
				}
				if (!function_decl_without_definition) {
					function_decl_without_definition = &func_decl;
				}
			}
		}

		return {
			function_decl_without_definition,
			function_decl_without_definition ? candidate_struct : nullptr};
	};

	if (auto found = find_in_struct(struct_info); found.first) {
		return found;
	}

	auto struct_type_it = getTypesByNameMap().find(struct_info->name);
	if (struct_type_it != getTypesByNameMap().end() && struct_type_it->second->isTemplateInstantiation()) {
		const TypeInfo* struct_type = struct_type_it->second;
		auto template_type_it = getTypesByNameMap().find(struct_type->baseTemplateName());
		if (template_type_it != getTypesByNameMap().end() && template_type_it->second->isStruct()) {
			if (auto found = find_in_struct(template_type_it->second->getStructInfo()); found.first) {
				return found;
			}
		}
	}

	return {nullptr, nullptr};
}

template <typename CallNodeT, typename RecurseFn>
std::vector<ASTNode> rebindFunctionCallTemplateArguments(
	const CallNodeT& call,
	RecurseFn&& recurse) {
	return transformCallTemplateArguments(call, recurse);
}

template <typename RecurseFn>
std::optional<ASTNode> tryRebindExpressionChildren(
	const ASTNode& node,
	RecurseFn&& recurse) {
	if (!node.is<ExpressionNode>()) {
		return std::nullopt;
	}

	const auto& expr = node.as<ExpressionNode>();
	if (std::holds_alternative<BinaryOperatorNode>(expr)) {
		const auto& binop = std::get<BinaryOperatorNode>(expr);
		return ASTNode::emplace_node<ExpressionNode>(BinaryOperatorNode(
			binop.get_token(),
			recurse(binop.get_lhs()),
			recurse(binop.get_rhs())));
	}

	if (std::holds_alternative<UnaryOperatorNode>(expr)) {
		const auto& unop = std::get<UnaryOperatorNode>(expr);
		return ASTNode::emplace_node<ExpressionNode>(UnaryOperatorNode(
			unop.get_token(),
			recurse(unop.get_operand()),
			unop.is_prefix(),
			unop.is_builtin_addressof()));
	}

	if (std::holds_alternative<TernaryOperatorNode>(expr)) {
		const auto& ternary = std::get<TernaryOperatorNode>(expr);
		return ASTNode::emplace_node<ExpressionNode>(TernaryOperatorNode(
			recurse(ternary.condition()),
			recurse(ternary.true_expr()),
			recurse(ternary.false_expr()),
			ternary.get_token()));
	}

	if (const auto* cast = std::get_if<StaticCastNode>(&expr)) {
		return ASTNode::emplace_node<ExpressionNode>(StaticCastNode(
			cast->target_type(),
			recurse(cast->expr()),
			cast->cast_token()));
	}

	if (const auto* cast = std::get_if<DynamicCastNode>(&expr)) {
		return ASTNode::emplace_node<ExpressionNode>(DynamicCastNode(
			cast->target_type(),
			recurse(cast->expr()),
			cast->cast_token()));
	}

	if (const auto* cast = std::get_if<ConstCastNode>(&expr)) {
		return ASTNode::emplace_node<ExpressionNode>(ConstCastNode(
			cast->target_type(),
			recurse(cast->expr()),
			cast->cast_token()));
	}

	if (const auto* cast = std::get_if<ReinterpretCastNode>(&expr)) {
		return ASTNode::emplace_node<ExpressionNode>(ReinterpretCastNode(
			cast->target_type(),
			recurse(cast->expr()),
			cast->cast_token()));
	}

	if (const auto* member_access = std::get_if<MemberAccessNode>(&expr)) {
		return ASTNode::emplace_node<ExpressionNode>(MemberAccessNode(
			recurse(member_access->object()),
			member_access->member_token(),
			member_access->is_arrow()));
	}

	if (const auto* subscript = std::get_if<ArraySubscriptNode>(&expr)) {
		return ASTNode::emplace_node<ExpressionNode>(ArraySubscriptNode(
			recurse(subscript->array_expr()),
			recurse(subscript->index_expr()),
			subscript->bracket_token()));
	}

	if (const auto* ptr_member_access = std::get_if<PointerToMemberAccessNode>(&expr)) {
		return ASTNode::emplace_node<ExpressionNode>(PointerToMemberAccessNode(
			recurse(ptr_member_access->object()),
			recurse(ptr_member_access->member_pointer()),
			ptr_member_access->operator_token(),
			ptr_member_access->is_arrow()));
	}

	if (std::holds_alternative<ConstructorCallNode>(expr)) {
		const auto& ctor_call = std::get<ConstructorCallNode>(expr);
		ChunkedVector<ASTNode> rebound_args;
		for (const auto& arg : ctor_call.arguments()) {
			rebound_args.push_back(recurse(arg));
		}
		return ASTNode::emplace_node<ExpressionNode>(ConstructorCallNode(
			ctor_call.type_node(),
			std::move(rebound_args),
			ctor_call.called_from()));
	}

	return std::nullopt;
}

template <typename RecurseFn>
std::optional<ASTNode> tryRebindNonExpressionNode(
	const ASTNode& node,
	RecurseFn&& recurse) {
	if (node.is<BlockNode>()) {
		ASTNode rebound_block = ASTNode::emplace_node<BlockNode>(BlockNode());
		auto& rebound_block_ref = rebound_block.as<BlockNode>();
		const auto& block = node.as<BlockNode>();
		rebound_block_ref.set_synthetic_decl_list(block.is_synthetic_decl_list());
		for (const auto& statement : block.get_statements()) {
			rebound_block_ref.add_statement_node(recurse(statement));
		}
		return rebound_block;
	}

	if (node.is<ReturnStatementNode>()) {
		const auto& return_stmt = node.as<ReturnStatementNode>();
		std::optional<ASTNode> rebound_expr;
		if (return_stmt.expression().has_value()) {
			rebound_expr = recurse(return_stmt.expression().value());
		}
		return ASTNode::emplace_node<ReturnStatementNode>(
			std::move(rebound_expr),
			return_stmt.return_token());
	}

	if (node.is<IfStatementNode>()) {
		const auto& if_stmt = node.as<IfStatementNode>();
		std::optional<ASTNode> rebound_else;
		if (if_stmt.has_else()) {
			rebound_else = recurse(if_stmt.get_else_statement().value());
		}
		std::optional<ASTNode> rebound_init;
		if (if_stmt.has_init()) {
			rebound_init = recurse(if_stmt.get_init_statement().value());
		}
		return ASTNode::emplace_node<IfStatementNode>(
			recurse(if_stmt.get_condition()),
			recurse(if_stmt.get_then_statement()),
			std::move(rebound_else),
			std::move(rebound_init),
			if_stmt.is_constexpr());
	}

	if (node.is<ForStatementNode>()) {
		const auto& for_stmt = node.as<ForStatementNode>();
		std::optional<ASTNode> rebound_init;
		if (for_stmt.has_init()) {
			rebound_init = recurse(for_stmt.get_init_statement().value());
		}
		std::optional<ASTNode> rebound_condition;
		if (for_stmt.has_condition()) {
			rebound_condition = recurse(for_stmt.get_condition().value());
		}
		std::optional<ASTNode> rebound_update;
		if (for_stmt.has_update()) {
			rebound_update = recurse(for_stmt.get_update_expression().value());
		}
		return ASTNode::emplace_node<ForStatementNode>(
			std::move(rebound_init),
			std::move(rebound_condition),
			std::move(rebound_update),
			recurse(for_stmt.get_body_statement()));
	}

	if (node.is<WhileStatementNode>()) {
		const auto& while_stmt = node.as<WhileStatementNode>();
		return ASTNode::emplace_node<WhileStatementNode>(
			recurse(while_stmt.get_condition()),
			recurse(while_stmt.get_body_statement()));
	}

	if (node.is<DoWhileStatementNode>()) {
		const auto& do_while_stmt = node.as<DoWhileStatementNode>();
		return ASTNode::emplace_node<DoWhileStatementNode>(
			recurse(do_while_stmt.get_body_statement()),
			recurse(do_while_stmt.get_condition()));
	}

	if (node.is<RangedForStatementNode>()) {
		const auto& ranged_for = node.as<RangedForStatementNode>();
		std::optional<ASTNode> rebound_init;
		if (ranged_for.has_init_statement()) {
			rebound_init = recurse(ranged_for.get_init_statement().value());
		}

		ASTNode rebound_ranged_for = ASTNode::emplace_node<RangedForStatementNode>(
			recurse(ranged_for.get_loop_variable_decl()),
			recurse(ranged_for.get_range_expression()),
			recurse(ranged_for.get_body_statement()),
			std::move(rebound_init));
		rebound_ranged_for.as<RangedForStatementNode>().set_resolved_dereference_function(
			ranged_for.resolved_dereference_function());
		return rebound_ranged_for;
	}

	if (node.is<SwitchStatementNode>()) {
		const auto& switch_stmt = node.as<SwitchStatementNode>();
		return ASTNode::emplace_node<SwitchStatementNode>(
			recurse(switch_stmt.get_condition()),
			recurse(switch_stmt.get_body()));
	}

	if (node.is<CaseLabelNode>()) {
		const auto& case_label = node.as<CaseLabelNode>();
		std::optional<ASTNode> rebound_stmt;
		if (case_label.has_statement()) {
			rebound_stmt = recurse(case_label.get_statement().value());
		}
		return ASTNode::emplace_node<CaseLabelNode>(
			recurse(case_label.get_case_value()),
			std::move(rebound_stmt));
	}

	if (node.is<DefaultLabelNode>()) {
		const auto& default_label = node.as<DefaultLabelNode>();
		std::optional<ASTNode> rebound_stmt;
		if (default_label.has_statement()) {
			rebound_stmt = recurse(default_label.get_statement().value());
		}
		return ASTNode::emplace_node<DefaultLabelNode>(std::move(rebound_stmt));
	}

	if (node.is<ThrowStatementNode>()) {
		const auto& throw_stmt = node.as<ThrowStatementNode>();
		if (throw_stmt.is_rethrow()) {
			return node;
		}
		return ASTNode::emplace_node<ThrowStatementNode>(
			recurse(throw_stmt.expression().value()),
			throw_stmt.throw_token());
	}

	if (node.is<CatchClauseNode>()) {
		const auto& catch_clause = node.as<CatchClauseNode>();
		std::optional<ASTNode> rebound_exception_decl;
		if (catch_clause.exception_declaration().has_value()) {
			rebound_exception_decl = recurse(catch_clause.exception_declaration().value());
		}

		if (catch_clause.is_catch_all()) {
			return ASTNode::emplace_node<CatchClauseNode>(
				recurse(catch_clause.body()),
				catch_clause.catch_token(),
				true);
		}

		return ASTNode::emplace_node<CatchClauseNode>(
			std::move(rebound_exception_decl),
			recurse(catch_clause.body()),
			catch_clause.catch_token());
	}

	if (node.is<TryStatementNode>()) {
		const auto& try_stmt = node.as<TryStatementNode>();
		std::vector<ASTNode> rebound_catches;
		rebound_catches.reserve(try_stmt.catch_clauses().size());
		for (const auto& catch_clause : try_stmt.catch_clauses()) {
			rebound_catches.push_back(recurse(catch_clause));
		}

		ASTNode rebound_try = ASTNode::emplace_node<TryStatementNode>(
			recurse(try_stmt.try_block()),
			std::move(rebound_catches),
			try_stmt.try_token());
		if (try_stmt.is_ctor_dtor_function_try()) {
			rebound_try.as<TryStatementNode>().set_is_ctor_dtor_function_try();
		}
		return rebound_try;
	}

	if (node.is<VariableDeclarationNode>()) {
		const auto& var_decl = node.as<VariableDeclarationNode>();
		std::optional<ASTNode> rebound_initializer;
		if (var_decl.initializer().has_value()) {
			rebound_initializer = recurse(var_decl.initializer().value());
		}
		ASTNode rebound_var = ASTNode::emplace_node<VariableDeclarationNode>(
			var_decl.declaration_node(),
			std::move(rebound_initializer),
			var_decl.storage_class());
		auto& rebound_var_ref = rebound_var.as<VariableDeclarationNode>();
		rebound_var_ref.set_is_constexpr(var_decl.is_constexpr());
		rebound_var_ref.set_is_constinit(var_decl.is_constinit());
		if (var_decl.has_outer_template_bindings()) {
			rebound_var_ref.set_outer_template_bindings(
				var_decl.outer_template_param_names(),
				var_decl.outer_template_args());
		}
		return rebound_var;
	}

	if (node.is<InitializerListNode>()) {
		const auto& init_list = node.as<InitializerListNode>();
		InitializerListNode rebound_list;
		for (size_t i = 0; i < init_list.size(); ++i) {
			ASTNode rebound_init = recurse(init_list.initializers()[i]);
			if (init_list.is_designated(i)) {
				rebound_list.add_designated_initializer(init_list.member_name(i), rebound_init);
			} else {
				rebound_list.add_initializer(rebound_init);
			}
		}
		return ASTNode::emplace_node<InitializerListNode>(rebound_list);
	}

	return std::nullopt;
}

namespace Detail {

template <typename VisitorFn>
bool visitASTImpl(const ASTNode& node, VisitorFn&& visitor) {
	if (!node.has_value()) {
		return false;
	}

	auto visit_child = [&](const ASTNode& child) {
		return visitASTImpl(child, visitor);
	};

	auto visit_direct_node = [&](const ASTNode& current) {
		if (visitor(current)) {
			return true;
		}

		if (current.is<BlockNode>()) {
			for (const auto& statement : current.as<BlockNode>().get_statements()) {
				if (visit_child(statement)) {
					return true;
				}
			}
			return false;
		}

		if (current.is<ReturnStatementNode>()) {
			const auto& return_stmt = current.as<ReturnStatementNode>();
			if (return_stmt.expression().has_value()) {
				return visit_child(return_stmt.expression().value());
			}
			return false;
		}

		if (current.is<IfStatementNode>()) {
			const auto& if_stmt = current.as<IfStatementNode>();
			if (if_stmt.has_init() && visit_child(if_stmt.get_init_statement().value())) {
				return true;
			}
			if (visit_child(if_stmt.get_condition())) {
				return true;
			}
			if (visit_child(if_stmt.get_then_statement())) {
				return true;
			}
			if (if_stmt.has_else() && visit_child(if_stmt.get_else_statement().value())) {
				return true;
			}
			return false;
		}

		if (current.is<ForStatementNode>()) {
			const auto& for_stmt = current.as<ForStatementNode>();
			if (for_stmt.has_init() && visit_child(for_stmt.get_init_statement().value())) {
				return true;
			}
			if (for_stmt.has_condition() && visit_child(for_stmt.get_condition().value())) {
				return true;
			}
			if (for_stmt.has_update() && visit_child(for_stmt.get_update_expression().value())) {
				return true;
			}
			return visit_child(for_stmt.get_body_statement());
		}

		if (current.is<WhileStatementNode>()) {
			const auto& while_stmt = current.as<WhileStatementNode>();
			return visit_child(while_stmt.get_condition()) ||
				   visit_child(while_stmt.get_body_statement());
		}

		if (current.is<DoWhileStatementNode>()) {
			const auto& do_while_stmt = current.as<DoWhileStatementNode>();
			return visit_child(do_while_stmt.get_body_statement()) ||
				   visit_child(do_while_stmt.get_condition());
		}

		if (current.is<RangedForStatementNode>()) {
			const auto& ranged_for = current.as<RangedForStatementNode>();
			if (ranged_for.has_init_statement() && visit_child(ranged_for.get_init_statement().value())) {
				return true;
			}
			return visit_child(ranged_for.get_loop_variable_decl()) ||
				   visit_child(ranged_for.get_range_expression()) ||
				   visit_child(ranged_for.get_body_statement());
		}

		if (current.is<SwitchStatementNode>()) {
			const auto& switch_stmt = current.as<SwitchStatementNode>();
			return visit_child(switch_stmt.get_condition()) ||
				   visit_child(switch_stmt.get_body());
		}

		if (current.is<CaseLabelNode>()) {
			const auto& case_label = current.as<CaseLabelNode>();
			if (visit_child(case_label.get_case_value())) {
				return true;
			}
			return case_label.has_statement() &&
				   visit_child(case_label.get_statement().value());
		}

		if (current.is<DefaultLabelNode>()) {
			const auto& default_label = current.as<DefaultLabelNode>();
			return default_label.has_statement() &&
				   visit_child(default_label.get_statement().value());
		}

		if (current.is<ThrowStatementNode>()) {
			const auto& throw_stmt = current.as<ThrowStatementNode>();
			return !throw_stmt.is_rethrow() && throw_stmt.expression().has_value() &&
				   visit_child(throw_stmt.expression().value());
		}

		if (current.is<CatchClauseNode>()) {
			const auto& catch_clause = current.as<CatchClauseNode>();
			if (catch_clause.exception_declaration().has_value() &&
				visit_child(catch_clause.exception_declaration().value())) {
				return true;
			}
			return visit_child(catch_clause.body());
		}

		if (current.is<TryStatementNode>()) {
			const auto& try_stmt = current.as<TryStatementNode>();
			if (visit_child(try_stmt.try_block())) {
				return true;
			}
			for (const auto& catch_clause : try_stmt.catch_clauses()) {
				if (visit_child(catch_clause)) {
					return true;
				}
			}
			return false;
		}

		if (current.is<VariableDeclarationNode>()) {
			const auto& var_decl = current.as<VariableDeclarationNode>();
			return var_decl.initializer().has_value() &&
				   visit_child(var_decl.initializer().value());
		}

		if (current.is<InitializerListNode>()) {
			const auto& init_list = current.as<InitializerListNode>();
			for (const auto& initializer : init_list.initializers()) {
				if (visit_child(initializer)) {
					return true;
				}
			}
			return false;
		}

		if (current.is<BinaryOperatorNode>()) {
			const auto& binop = current.as<BinaryOperatorNode>();
			return visit_child(binop.get_lhs()) ||
				   visit_child(binop.get_rhs());
		}

		if (current.is<UnaryOperatorNode>()) {
			return visit_child(current.as<UnaryOperatorNode>().get_operand());
		}

		if (current.is<TernaryOperatorNode>()) {
			const auto& ternary = current.as<TernaryOperatorNode>();
			return visit_child(ternary.condition()) ||
				   visit_child(ternary.true_expr()) ||
				   visit_child(ternary.false_expr());
		}

		if (current.is<StaticCastNode>()) {
			return visit_child(current.as<StaticCastNode>().expr());
		}

		if (current.is<DynamicCastNode>()) {
			return visit_child(current.as<DynamicCastNode>().expr());
		}

		if (current.is<ConstCastNode>()) {
			return visit_child(current.as<ConstCastNode>().expr());
		}

		if (current.is<ReinterpretCastNode>()) {
			return visit_child(current.as<ReinterpretCastNode>().expr());
		}

		if (current.is<CallExprNode>()) {
			const auto& call = current.as<CallExprNode>();
			if (call.has_receiver() && visit_child(call.receiver())) {
				return true;
			}
			for (const auto& argument : call.arguments()) {
				if (visit_child(argument)) {
					return true;
				}
			}
			return false;
		}

		if (current.is<ConstructorCallNode>()) {
			for (const auto& argument : current.as<ConstructorCallNode>().arguments()) {
				if (visit_child(argument)) {
					return true;
				}
			}
			return false;
		}

		if (current.is<MemberAccessNode>()) {
			return visit_child(current.as<MemberAccessNode>().object());
		}

		if (current.is<ArraySubscriptNode>()) {
			const auto& subscript = current.as<ArraySubscriptNode>();
			return visit_child(subscript.array_expr()) ||
				   visit_child(subscript.index_expr());
		}

		if (current.is<PointerToMemberAccessNode>()) {
			const auto& member_access = current.as<PointerToMemberAccessNode>();
			return visit_child(member_access.object()) ||
				   visit_child(member_access.member_pointer());
		}

		return false;
	};

	if (node.is<ExpressionNode>()) {
		return std::visit(
			[&](const auto& expr_node) {
				return visit_direct_node(ASTNode(&expr_node));
			},
			node.as<ExpressionNode>());
	}

	return visit_direct_node(node);
}

} // namespace Detail

template <typename Fn>
void visitAST(const ASTNode& node, Fn&& visitor) {
	Detail::visitASTImpl(node, [&](const ASTNode& current) {
		visitor(current);
		return false;
	});
}

template <typename PredicateFn>
bool visitASTUntil(const ASTNode& node, PredicateFn&& predicate) {
	return Detail::visitASTImpl(node, std::forward<PredicateFn>(predicate));
}

} // namespace RebindStaticMemberAst
