#pragma once

#include "AstNodeTypes_Stmt.h"
#include "AstNodeTypes_Template.h"
#include <type_traits>

namespace AstTraversal {

enum class VisitDecision {
	Continue,
	SkipChildren,
	Stop,
};

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
		VisitDecision decision = visitor(current);
		if (decision == VisitDecision::Stop) {
			return true;
		}
		if (decision == VisitDecision::SkipChildren) {
			return false;
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
			const auto& cast = current.as<StaticCastNode>();
			return visit_child(cast.target_type()) ||
				   visit_child(cast.expr());
		}

		if (current.is<DynamicCastNode>()) {
			const auto& cast = current.as<DynamicCastNode>();
			return visit_child(cast.target_type()) ||
				   visit_child(cast.expr());
		}

		if (current.is<ConstCastNode>()) {
			const auto& cast = current.as<ConstCastNode>();
			return visit_child(cast.target_type()) ||
				   visit_child(cast.expr());
		}

		if (current.is<ReinterpretCastNode>()) {
			const auto& cast = current.as<ReinterpretCastNode>();
			return visit_child(cast.target_type()) ||
				   visit_child(cast.expr());
		}

		if (current.is<SizeofExprNode>()) {
			return visit_child(current.as<SizeofExprNode>().type_or_expr());
		}

		if (current.is<AlignofExprNode>()) {
			return visit_child(current.as<AlignofExprNode>().type_or_expr());
		}

		if (current.is<TypeTraitExprNode>()) {
			const auto& trait = current.as<TypeTraitExprNode>();
			if (trait.type_node().has_value() && visit_child(trait.type_node())) {
				return true;
			}
			if (trait.has_second_type() && visit_child(trait.second_type_node())) {
				return true;
			}
			for (const auto& type_node : trait.additional_type_nodes()) {
				if (visit_child(type_node)) {
					return true;
				}
			}
			return false;
		}

		if (current.is<NewExpressionNode>()) {
			const auto& new_expr = current.as<NewExpressionNode>();
			if (visit_child(new_expr.type_node())) {
				return true;
			}
			if (new_expr.size_expr().has_value() && visit_child(new_expr.size_expr().value())) {
				return true;
			}
			for (const auto& constructor_arg : new_expr.constructor_args()) {
				if (visit_child(constructor_arg)) {
					return true;
				}
			}
			for (const auto& placement_arg : new_expr.placement_args()) {
				if (visit_child(placement_arg)) {
					return true;
				}
			}
			return false;
		}

		if (current.is<TypeidNode>()) {
			return visit_child(current.as<TypeidNode>().operand());
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

template <typename Visitor>
bool visitStandaloneExpressionNode(const ASTNode& expr, Visitor&&) {
	(void)expr;
	return false;
}

template <typename NodeType, typename... RemainingNodeTypes, typename Visitor>
bool visitStandaloneExpressionNode(const ASTNode& expr, Visitor&& visitor) {
	if (expr.is<NodeType>()) {
		return visitor(expr.as<NodeType>());
	}

	if constexpr (sizeof...(RemainingNodeTypes) > 0) {
		return visitStandaloneExpressionNode<RemainingNodeTypes...>(expr, visitor);
	}

	return false;
}

} // namespace Detail

template <typename Fn>
void visitAST(const ASTNode& node, Fn&& visitor) {
	Detail::visitASTImpl(node, [&](const ASTNode& current) {
		visitor(current);
		return VisitDecision::Continue;
	});
}

template <typename PredicateFn>
bool visitASTUntil(const ASTNode& node, PredicateFn&& predicate) {
	return Detail::visitASTImpl(node, [&](const ASTNode& current) {
		return predicate(current) ? VisitDecision::Stop : VisitDecision::Continue;
	});
}

template <typename VisitorFn>
bool visitASTWithDecisions(const ASTNode& node, VisitorFn&& visitor) {
	return Detail::visitASTImpl(node, std::forward<VisitorFn>(visitor));
}

template <typename Visitor>
bool visitExpressionNode(const ASTNode& expr, Visitor&& visitor) {
	if (expr.is<ExpressionNode>()) {
		return std::visit(visitor, expr.as<ExpressionNode>());
	}

	return Detail::visitStandaloneExpressionNode<
		IdentifierNode,
		QualifiedIdentifierNode,
		StringLiteralNode,
		NumericLiteralNode,
		BoolLiteralNode,
		BinaryOperatorNode,
		UnaryOperatorNode,
		TernaryOperatorNode,
		ConstructorCallNode,
		MemberAccessNode,
		PointerToMemberAccessNode,
		ArraySubscriptNode,
		SizeofExprNode,
		SizeofPackNode,
		AlignofExprNode,
		OffsetofExprNode,
		TypeTraitExprNode,
		NewExpressionNode,
		DeleteExpressionNode,
		StaticCastNode,
		DynamicCastNode,
		ConstCastNode,
		ReinterpretCastNode,
		TypeidNode,
		LambdaExpressionNode,
		TemplateParameterReferenceNode,
		FoldExpressionNode,
		PackExpansionExprNode,
		PseudoDestructorCallNode,
		NoexceptExprNode,
		InitializerListConstructionNode,
		ThrowExpressionNode,
		CallExprNode>(expr, visitor);
}

} // namespace AstTraversal
