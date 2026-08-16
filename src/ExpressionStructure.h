#pragma once

#include "AstNodeTypes.h"

// This header describes expression structure only.  It deliberately does not
// perform substitution, overload resolution, evaluation, or lowering.
namespace ExpressionStructure {

enum class ExpressionChildRole {
	Operand,
	Condition,
	TrueBranch,
	FalseBranch,
	Receiver,
	TemplateArgument,
	CallArgument,
	ConstructorArgument,
	PlacementArgument,
	ConstructedType,
	ArrayBound,
	TypeOperand,
	TypeArgument,
	CastTarget,
	CaptureInitializer,
	LambdaParameter,
	LambdaReturnType,
	Body,
	FoldInitializer,
	FoldPackExpression,
	PackPattern,
	InitializerElementType,
	InitializerTargetType,
	InitializerElement,
};

namespace Detail {

template <typename T>
inline constexpr bool dependent_false_v = false;

template <typename Visitor>
void emitChild(Visitor& visitor, ExpressionChildRole role, const ASTNode& child) {
	visitor(role, child);
}

template <typename Visitor, typename Child>
void emitChild(Visitor& visitor, ExpressionChildRole role, const Child& child) {
	visitor(role, ASTNode(&child));
}

template <typename T, typename Visitor>
void visitExpressionChildrenForType(const T& expression, Visitor& visitor) {
	using ExpressionType = std::remove_cvref_t<T>;

	if constexpr (std::is_same_v<ExpressionType, IdentifierNode> ||
				  std::is_same_v<ExpressionType, StringLiteralNode> ||
				  std::is_same_v<ExpressionType, NumericLiteralNode> ||
				  std::is_same_v<ExpressionType, BoolLiteralNode> ||
				  std::is_same_v<ExpressionType, SizeofPackNode> ||
				  std::is_same_v<ExpressionType, TemplateParameterReferenceNode>) {
		// Explicit leaf classification.  This branch is intentionally not a
		// catch-all: a new ExpressionNode alternative must be declared below.
		(void)expression;
		(void)visitor;
	} else if constexpr (std::is_same_v<ExpressionType, QualifiedIdentifierNode>) {
		for (const auto& argument : expression.template_arguments()) {
			emitChild(visitor, ExpressionChildRole::TemplateArgument, argument);
		}
	} else if constexpr (std::is_same_v<ExpressionType, BinaryOperatorNode>) {
		emitChild(visitor, ExpressionChildRole::Operand, expression.get_lhs());
		emitChild(visitor, ExpressionChildRole::Operand, expression.get_rhs());
	} else if constexpr (std::is_same_v<ExpressionType, UnaryOperatorNode>) {
		emitChild(visitor, ExpressionChildRole::Operand, expression.get_operand());
	} else if constexpr (std::is_same_v<ExpressionType, TernaryOperatorNode>) {
		emitChild(visitor, ExpressionChildRole::Condition, expression.condition());
		emitChild(visitor, ExpressionChildRole::TrueBranch, expression.true_expr());
		emitChild(visitor, ExpressionChildRole::FalseBranch, expression.false_expr());
	} else if constexpr (std::is_same_v<ExpressionType, ConstructorCallNode>) {
		emitChild(visitor, ExpressionChildRole::ConstructedType, expression.type_node());
		for (const auto& argument : expression.arguments()) {
			emitChild(visitor, ExpressionChildRole::ConstructorArgument, argument);
		}
	} else if constexpr (std::is_same_v<ExpressionType, MemberAccessNode>) {
		emitChild(visitor, ExpressionChildRole::Receiver, expression.object());
	} else if constexpr (std::is_same_v<ExpressionType, PointerToMemberAccessNode>) {
		emitChild(visitor, ExpressionChildRole::Receiver, expression.object());
		emitChild(visitor, ExpressionChildRole::Operand, expression.member_pointer());
	} else if constexpr (std::is_same_v<ExpressionType, ArraySubscriptNode>) {
		emitChild(visitor, ExpressionChildRole::Operand, expression.array_expr());
		emitChild(visitor, ExpressionChildRole::Operand, expression.index_expr());
	} else if constexpr (std::is_same_v<ExpressionType, SizeofExprNode>) {
		emitChild(visitor,
				  expression.is_type() ? ExpressionChildRole::TypeOperand : ExpressionChildRole::Operand,
				  expression.type_or_expr());
	} else if constexpr (std::is_same_v<ExpressionType, AlignofExprNode>) {
		emitChild(visitor,
				  expression.is_type() ? ExpressionChildRole::TypeOperand : ExpressionChildRole::Operand,
				  expression.type_or_expr());
	} else if constexpr (std::is_same_v<ExpressionType, OffsetofExprNode>) {
		emitChild(visitor, ExpressionChildRole::TypeOperand, expression.type_node());
	} else if constexpr (std::is_same_v<ExpressionType, TypeTraitExprNode>) {
		if (expression.type_node().has_value()) {
			emitChild(visitor, ExpressionChildRole::TypeArgument, expression.type_node());
		}
		if (expression.has_second_type()) {
			emitChild(visitor, ExpressionChildRole::TypeArgument, expression.second_type_node());
		}
		for (const auto& type_node : expression.additional_type_nodes()) {
			emitChild(visitor, ExpressionChildRole::TypeArgument, type_node);
		}
	} else if constexpr (std::is_same_v<ExpressionType, NewExpressionNode>) {
		emitChild(visitor, ExpressionChildRole::ConstructedType, expression.type_node());
		if (expression.size_expr().has_value()) {
			emitChild(visitor, ExpressionChildRole::ArrayBound, *expression.size_expr());
		}
		for (const auto& argument : expression.constructor_args()) {
			emitChild(visitor, ExpressionChildRole::ConstructorArgument, argument);
		}
		for (const auto& argument : expression.placement_args()) {
			emitChild(visitor, ExpressionChildRole::PlacementArgument, argument);
		}
	} else if constexpr (std::is_same_v<ExpressionType, DeleteExpressionNode>) {
		emitChild(visitor, ExpressionChildRole::Operand, expression.expr());
	} else if constexpr (std::is_same_v<ExpressionType, StaticCastNode> ||
				  std::is_same_v<ExpressionType, DynamicCastNode> ||
				  std::is_same_v<ExpressionType, ConstCastNode> ||
				  std::is_same_v<ExpressionType, ReinterpretCastNode>) {
		emitChild(visitor, ExpressionChildRole::CastTarget, expression.target_type());
		emitChild(visitor, ExpressionChildRole::Operand, expression.expr());
	} else if constexpr (std::is_same_v<ExpressionType, TypeidNode>) {
		emitChild(visitor,
				  expression.is_type() ? ExpressionChildRole::TypeOperand : ExpressionChildRole::Operand,
				  expression.operand());
	} else if constexpr (std::is_same_v<ExpressionType, LambdaExpressionNode>) {
		for (const auto& capture : expression.captures()) {
			if (capture.has_initializer()) {
				emitChild(visitor, ExpressionChildRole::CaptureInitializer, *capture.initializer());
			}
		}
		for (const auto& parameter : expression.parameters()) {
			emitChild(visitor, ExpressionChildRole::LambdaParameter, parameter);
		}
		if (expression.return_type().has_value()) {
			emitChild(visitor, ExpressionChildRole::LambdaReturnType, *expression.return_type());
		}
		emitChild(visitor, ExpressionChildRole::Body, expression.body());
	} else if constexpr (std::is_same_v<ExpressionType, FoldExpressionNode>) {
		if (expression.init_expr().has_value()) {
			emitChild(visitor, ExpressionChildRole::FoldInitializer, *expression.init_expr());
		}
		if (expression.pack_expr().has_value()) {
			emitChild(visitor, ExpressionChildRole::FoldPackExpression, *expression.pack_expr());
		}
	} else if constexpr (std::is_same_v<ExpressionType, PackExpansionExprNode>) {
		emitChild(visitor, ExpressionChildRole::PackPattern, expression.pattern());
	} else if constexpr (std::is_same_v<ExpressionType, PseudoDestructorCallNode>) {
		emitChild(visitor, ExpressionChildRole::Receiver, expression.object());
	} else if constexpr (std::is_same_v<ExpressionType, NoexceptExprNode>) {
		emitChild(visitor, ExpressionChildRole::Operand, expression.expr());
	} else if constexpr (std::is_same_v<ExpressionType, InitializerListConstructionNode>) {
		emitChild(visitor, ExpressionChildRole::InitializerElementType, expression.element_type());
		emitChild(visitor, ExpressionChildRole::InitializerTargetType, expression.target_type());
		for (const auto& element : expression.elements()) {
			emitChild(visitor, ExpressionChildRole::InitializerElement, element);
		}
	} else if constexpr (std::is_same_v<ExpressionType, ThrowExpressionNode>) {
		if (expression.expression().has_value()) {
			emitChild(visitor, ExpressionChildRole::Operand, *expression.expression());
		}
	} else if constexpr (std::is_same_v<ExpressionType, CallExprNode>) {
		if (expression.has_receiver()) {
			emitChild(visitor, ExpressionChildRole::Receiver, expression.receiver());
		}
		for (const auto& argument : expression.template_arguments()) {
			emitChild(visitor, ExpressionChildRole::TemplateArgument, argument);
		}
		for (const auto& argument : expression.arguments()) {
			emitChild(visitor, ExpressionChildRole::CallArgument, argument);
		}
	} else {
		static_assert(dependent_false_v<ExpressionType>,
			"Every ExpressionNode alternative must declare its structural children or leaf status");
	}
}

template <size_t Index, typename Visitor>
bool visitDirectExpressionChildren(const ASTNode& node, Visitor& visitor) {
	if constexpr (Index == std::variant_size_v<ExpressionNode>) {
		return false;
	} else {
		using ExpressionType = std::variant_alternative_t<Index, ExpressionNode>;
		if (node.is<ExpressionType>()) {
			visitExpressionChildrenForType(node.as<ExpressionType>(), visitor);
			return true;
		}
		return visitDirectExpressionChildren<Index + 1>(node, visitor);
	}
}

} // namespace Detail

template <typename Visitor>
bool visitExpressionChildren(const ExpressionNode& expression, Visitor&& visitor) {
	auto&& visitor_ref = visitor;
	std::visit(
		[&visitor_ref](const auto& expression_type) {
			Detail::visitExpressionChildrenForType(expression_type, visitor_ref);
		},
		expression);
	return true;
}

// ASTNode roots may contain an ExpressionNode wrapper or a legacy direct
// expression alternative.  Both forms use the same structural dispatch.
template <typename Visitor>
bool visitExpressionChildren(const ASTNode& node, Visitor&& visitor) {
	auto&& visitor_ref = visitor;
	if (node.is<ExpressionNode>()) {
		return visitExpressionChildren(node.as<ExpressionNode>(), visitor_ref);
	}
	return Detail::visitDirectExpressionChildren<0>(node, visitor_ref);
}

} // namespace ExpressionStructure
