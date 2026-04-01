#pragma once

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

		for (const auto& member_func : candidate_struct->member_functions) {
			if (member_func.getName() != function_name || !member_func.function_decl.is<FunctionDeclarationNode>()) {
				continue;
			}

			const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
			if (func_decl.is_static()) {
				return {&func_decl, candidate_struct};
			}
		}

		return {nullptr, nullptr};
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

template <typename RecurseFn>
std::vector<ASTNode> rebindFunctionCallTemplateArguments(
	const FunctionCallNode& call,
	RecurseFn&& recurse) {
	std::vector<ASTNode> rebound_template_args;
	if (!call.has_template_arguments()) {
		return rebound_template_args;
	}

	rebound_template_args.reserve(call.template_arguments().size());
	for (const auto& template_arg : call.template_arguments()) {
		rebound_template_args.push_back(recurse(template_arg));
	}
	return rebound_template_args;
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

template <typename Fn>
void visitAST(const ASTNode& node, Fn&& visitor) {
	if (!node.has_value()) {
		return;
	}

	auto visit_child = [&](const ASTNode& child) {
		visitAST(child, visitor);
	};

	auto visit_direct_node = [&](const ASTNode& current) {
		visitor(current);

		if (current.is<BlockNode>()) {
			for (const auto& statement : current.as<BlockNode>().get_statements()) {
				visit_child(statement);
			}
			return;
		}

		if (current.is<ReturnStatementNode>()) {
			const auto& return_stmt = current.as<ReturnStatementNode>();
			if (return_stmt.expression().has_value()) {
				visit_child(return_stmt.expression().value());
			}
			return;
		}

		if (current.is<IfStatementNode>()) {
			const auto& if_stmt = current.as<IfStatementNode>();
			if (if_stmt.has_init()) {
				visit_child(if_stmt.get_init_statement().value());
			}
			visit_child(if_stmt.get_condition());
			visit_child(if_stmt.get_then_statement());
			if (if_stmt.has_else()) {
				visit_child(if_stmt.get_else_statement().value());
			}
			return;
		}

		if (current.is<ForStatementNode>()) {
			const auto& for_stmt = current.as<ForStatementNode>();
			if (for_stmt.has_init()) {
				visit_child(for_stmt.get_init_statement().value());
			}
			if (for_stmt.has_condition()) {
				visit_child(for_stmt.get_condition().value());
			}
			if (for_stmt.has_update()) {
				visit_child(for_stmt.get_update_expression().value());
			}
			visit_child(for_stmt.get_body_statement());
			return;
		}

		if (current.is<WhileStatementNode>()) {
			const auto& while_stmt = current.as<WhileStatementNode>();
			visit_child(while_stmt.get_condition());
			visit_child(while_stmt.get_body_statement());
			return;
		}

		if (current.is<DoWhileStatementNode>()) {
			const auto& do_while_stmt = current.as<DoWhileStatementNode>();
			visit_child(do_while_stmt.get_body_statement());
			visit_child(do_while_stmt.get_condition());
			return;
		}

		if (current.is<SwitchStatementNode>()) {
			const auto& switch_stmt = current.as<SwitchStatementNode>();
			visit_child(switch_stmt.get_condition());
			visit_child(switch_stmt.get_body());
			return;
		}

		if (current.is<CaseLabelNode>()) {
			const auto& case_label = current.as<CaseLabelNode>();
			visit_child(case_label.get_case_value());
			if (case_label.has_statement()) {
				visit_child(case_label.get_statement().value());
			}
			return;
		}

		if (current.is<DefaultLabelNode>()) {
			const auto& default_label = current.as<DefaultLabelNode>();
			if (default_label.has_statement()) {
				visit_child(default_label.get_statement().value());
			}
			return;
		}

		if (current.is<ThrowStatementNode>()) {
			const auto& throw_stmt = current.as<ThrowStatementNode>();
			if (!throw_stmt.is_rethrow()) {
				visit_child(throw_stmt.expression().value());
			}
			return;
		}

		if (current.is<VariableDeclarationNode>()) {
			const auto& var_decl = current.as<VariableDeclarationNode>();
			if (var_decl.initializer().has_value()) {
				visit_child(var_decl.initializer().value());
			}
			return;
		}

		if (current.is<InitializerListNode>()) {
			const auto& init_list = current.as<InitializerListNode>();
			for (const auto& initializer : init_list.initializers()) {
				visit_child(initializer);
			}
			return;
		}

		if (current.is<BinaryOperatorNode>()) {
			const auto& binop = current.as<BinaryOperatorNode>();
			visit_child(binop.get_lhs());
			visit_child(binop.get_rhs());
			return;
		}

		if (current.is<UnaryOperatorNode>()) {
			visit_child(current.as<UnaryOperatorNode>().get_operand());
			return;
		}

		if (current.is<TernaryOperatorNode>()) {
			const auto& ternary = current.as<TernaryOperatorNode>();
			visit_child(ternary.condition());
			visit_child(ternary.true_expr());
			visit_child(ternary.false_expr());
			return;
		}

		if (current.is<StaticCastNode>()) {
			visit_child(current.as<StaticCastNode>().expr());
			return;
		}

		if (current.is<DynamicCastNode>()) {
			visit_child(current.as<DynamicCastNode>().expr());
			return;
		}

		if (current.is<ConstCastNode>()) {
			visit_child(current.as<ConstCastNode>().expr());
			return;
		}

		if (current.is<ReinterpretCastNode>()) {
			visit_child(current.as<ReinterpretCastNode>().expr());
			return;
		}

		if (current.is<FunctionCallNode>()) {
			for (const auto& argument : current.as<FunctionCallNode>().arguments()) {
				visit_child(argument);
			}
			return;
		}

		if (current.is<MemberFunctionCallNode>()) {
			const auto& member_call = current.as<MemberFunctionCallNode>();
			visit_child(member_call.object());
			for (const auto& argument : member_call.arguments()) {
				visit_child(argument);
			}
			return;
		}

		if (current.is<ConstructorCallNode>()) {
			for (const auto& argument : current.as<ConstructorCallNode>().arguments()) {
				visit_child(argument);
			}
			return;
		}

		if (current.is<MemberAccessNode>()) {
			visit_child(current.as<MemberAccessNode>().object());
			return;
		}

		if (current.is<ArraySubscriptNode>()) {
			const auto& subscript = current.as<ArraySubscriptNode>();
			visit_child(subscript.array_expr());
			visit_child(subscript.index_expr());
			return;
		}

		if (current.is<PointerToMemberAccessNode>()) {
			const auto& member_access = current.as<PointerToMemberAccessNode>();
			visit_child(member_access.object());
			visit_child(member_access.member_pointer());
			return;
		}
	};

	if (node.is<ExpressionNode>()) {
		std::visit(
			[&](const auto& expr_node) {
				visit_direct_node(ASTNode(&expr_node));
			},
			node.as<ExpressionNode>());
		return;
	}

	visit_direct_node(node);
}

} // namespace RebindStaticMemberAst
