#pragma once

#include "AstNodeTypes.h"
#include "CallNodeHelpers.h"
#include "ExpressionStructure.h"

namespace ExpressionRewriterDetail {

template <typename T>
inline constexpr bool dependent_false_v = false;

} // namespace ExpressionRewriterDetail

// Rebuilds expression structure after a caller has rewritten its children.
// This class deliberately knows nothing about substitution, lookup, evaluation,
// ownership, or lowering.  The callbacks are the policy boundary:
// one_to_one rewrites a single child, while zero_to_many can erase or expand an
// argument in a sequence.
class ExpressionRewriter {
public:
	template <typename OneToOne, typename ZeroToMany>
	ASTNode rewrite(
		const ASTNode& expression,
		OneToOne&& one_to_one,
		ZeroToMany&& zero_to_many) const {
		auto&& one_to_one_ref = one_to_one;
		auto&& zero_to_many_ref = zero_to_many;
		if (!expression.has_value()) {
			return expression;
		}
		if (expression.is<ExpressionNode>()) {
			return std::visit(
				[&](const auto& node) {
					return rewriteExpressionNode(node, one_to_one_ref, zero_to_many_ref);
				},
				expression.as<ExpressionNode>());
		}
		if (expression.is<TypeSpecifierNode>()) {
			return one_to_one_ref(
				ASTNode(&expression.as<TypeSpecifierNode>()),
				ExpressionStructure::ExpressionChildRole::TypeOperand);
		}
		return rewriteDirectExpression<0>(
			expression,
			one_to_one_ref,
			zero_to_many_ref);
	}

private:
	using Role = ExpressionStructure::ExpressionChildRole;

	template <size_t Index, typename OneToOne, typename ZeroToMany>
	ASTNode rewriteDirectExpression(
		const ASTNode& expression,
		OneToOne& one_to_one,
		ZeroToMany& zero_to_many) const {
		if constexpr (Index == std::variant_size_v<ExpressionNode>) {
			throw InternalError(
				"ExpressionRewriter received a non-expression AST node: " +
				std::string(expression.type_name()));
		} else {
			using T = std::variant_alternative_t<Index, ExpressionNode>;
			if (expression.is<T>()) {
				ExpressionNode wrapped(expression.as<T>());
				ASTNode root = ASTNode::emplace_node<ExpressionNode>(
					std::move(wrapped));
				return rewrite(root, one_to_one, zero_to_many);
			}
			return rewriteDirectExpression<Index + 1>(
				expression,
				one_to_one,
				zero_to_many);
		}
	}

	template <typename OneToOne>
	static ASTNode rewriteOne(
		const ASTNode& child,
		Role role,
		OneToOne& one_to_one) {
		return one_to_one(child, role);
	}

	template <typename OneToOne>
	static TypeSpecifierNode rewriteType(
		const TypeSpecifierNode& type,
		Role role,
		OneToOne& one_to_one) {
		ASTNode rewritten = rewriteOne(ASTNode(&type), role, one_to_one);
		if (!rewritten.is<TypeSpecifierNode>()) {
			throw InternalError(
				"ExpressionRewriter type child rewrite did not return TypeSpecifierNode");
		}
		return rewritten.as<TypeSpecifierNode>();
	}

	template <typename OneToOne>
	static TypeSpecifierNode rewriteType(
		const ASTNode& type,
		Role role,
		OneToOne& one_to_one) {
		ASTNode rewritten = rewriteOne(type, role, one_to_one);
		if (!rewritten.is<TypeSpecifierNode>()) {
			throw InternalError(
				"ExpressionRewriter type child rewrite did not return TypeSpecifierNode");
		}
		return rewritten.as<TypeSpecifierNode>();
	}

	template <typename Source, typename ZeroToMany>
	static std::vector<ASTNode> rewriteSequence(
		const Source& source,
		Role role,
		ZeroToMany& zero_to_many) {
		std::vector<ASTNode> result;
		for (const ASTNode& child : source) {
			zero_to_many(child, role, result);
		}
		return result;
	}

	template <typename OneToOne, typename ZeroToMany>
	static ASTNode wrap(ExpressionNode expression) {
		ExpressionNode& stored =
			gChunkedAnyStorage.emplace_back<ExpressionNode>(std::move(expression));
		return ASTNode(&stored);
	}

	template <typename OneToOne, typename ZeroToMany>
	static ASTNode rewriteExpressionNode(
		const auto& node,
		OneToOne& one_to_one,
		ZeroToMany& zero_to_many) {
		using T = std::remove_cvref_t<decltype(node)>;

		if constexpr (std::is_same_v<T, IdentifierNode> ||
					  std::is_same_v<T, StringLiteralNode> ||
					  std::is_same_v<T, NumericLiteralNode> ||
					  std::is_same_v<T, BoolLiteralNode> ||
					  std::is_same_v<T, SizeofPackNode> ||
					  std::is_same_v<T, TemplateParameterReferenceNode>) {
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(node));
		} else if constexpr (std::is_same_v<T, QualifiedIdentifierNode>) {
			QualifiedIdentifierNode rewritten(node.namespace_handle(), node.identifier_token());
			if (node.hasDependentQualifiedName()) {
				rewritten.setDependentQualifiedName(*node.dependentQualifiedName());
			}
			std::vector<ASTNode> template_arguments;
			for (const ASTNode& argument : node.template_arguments()) {
				template_arguments.push_back(rewriteOne(argument, Role::TemplateArgument, one_to_one));
			}
			if (!template_arguments.empty()) {
				rewritten.set_template_arguments(std::move(template_arguments));
			}
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(std::move(rewritten)));
		} else if constexpr (std::is_same_v<T, BinaryOperatorNode>) {
			BinaryOperatorNode rewritten(
				node.get_token(),
				rewriteOne(node.get_lhs(), Role::Operand, one_to_one),
				rewriteOne(node.get_rhs(), Role::Operand, one_to_one));
			rewritten.copy_semantic_operator_resolution_from(node);
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(std::move(rewritten)));
		} else if constexpr (std::is_same_v<T, UnaryOperatorNode>) {
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(UnaryOperatorNode(
				node.get_token(),
				rewriteOne(node.get_operand(), Role::Operand, one_to_one),
				node.is_prefix(),
				node.is_builtin_addressof())));
		} else if constexpr (std::is_same_v<T, TernaryOperatorNode>) {
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(TernaryOperatorNode(
				rewriteOne(node.condition(), Role::Condition, one_to_one),
				rewriteOne(node.true_expr(), Role::TrueBranch, one_to_one),
				rewriteOne(node.false_expr(), Role::FalseBranch, one_to_one),
				node.get_token())));
		} else if constexpr (std::is_same_v<T, ConstructorCallNode>) {
			TypeSpecifierNode type = rewriteType(node.type_node(), Role::ConstructedType, one_to_one);
			std::vector<ASTNode> arguments = rewriteSequence(
				node.arguments(), Role::ConstructorArgument, zero_to_many);
			ChunkedVector<ASTNode> stored_arguments;
			for (const ASTNode& argument : arguments) {
				stored_arguments.push_back(argument);
			}
			ConstructorCallNode rewritten(type, std::move(stored_arguments), node.called_from());
			rewritten.set_resolved_constructor(node.resolved_constructor());
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(std::move(rewritten)));
		} else if constexpr (std::is_same_v<T, MemberAccessNode>) {
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(MemberAccessNode(
				rewriteOne(node.object(), Role::Receiver, one_to_one),
				node.member_token(),
				node.is_arrow())));
		} else if constexpr (std::is_same_v<T, PointerToMemberAccessNode>) {
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(PointerToMemberAccessNode(
				rewriteOne(node.object(), Role::Receiver, one_to_one),
				rewriteOne(node.member_pointer(), Role::Operand, one_to_one),
				node.operator_token(),
				node.is_arrow())));
		} else if constexpr (std::is_same_v<T, ArraySubscriptNode>) {
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(ArraySubscriptNode(
				rewriteOne(node.array_expr(), Role::Operand, one_to_one),
				rewriteOne(node.index_expr(), Role::Operand, one_to_one),
				node.bracket_token())));
		} else if constexpr (std::is_same_v<T, SizeofExprNode>) {
			ASTNode operand = rewriteOne(
				node.type_or_expr(),
				node.is_type() ? Role::TypeOperand : Role::Operand,
				one_to_one);
			SizeofExprNode rewritten = node.is_type()
				? SizeofExprNode(operand, node.sizeof_token())
				: SizeofExprNode::from_expression(operand, node.sizeof_token());
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(std::move(rewritten)));
		} else if constexpr (std::is_same_v<T, AlignofExprNode>) {
			ASTNode operand = rewriteOne(
				node.type_or_expr(),
				node.is_type() ? Role::TypeOperand : Role::Operand,
				one_to_one);
			AlignofExprNode rewritten = node.is_type()
				? AlignofExprNode(operand, node.alignof_token())
				: AlignofExprNode::from_expression(operand, node.alignof_token());
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(std::move(rewritten)));
		} else if constexpr (std::is_same_v<T, OffsetofExprNode>) {
			std::vector<Token> member_path(node.member_path().begin(), node.member_path().end());
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(OffsetofExprNode(
				rewriteType(node.type_node(), Role::TypeOperand, one_to_one),
				std::move(member_path),
				node.offsetof_token())));
		} else if constexpr (std::is_same_v<T, TypeTraitExprNode>) {
			if (node.is_no_arg_trait()) {
				return wrap<OneToOne, ZeroToMany>(ExpressionNode(TypeTraitExprNode(node.kind(), node.trait_token())));
			}
			ASTNode type = rewriteOne(node.type_node(), Role::TypeArgument, one_to_one);
			if (node.has_second_type()) {
				return wrap<OneToOne, ZeroToMany>(ExpressionNode(TypeTraitExprNode(
					node.kind(),
					type,
					rewriteOne(node.second_type_node(), Role::TypeArgument, one_to_one),
					node.trait_token())));
			}
			if (node.is_variadic_trait()) {
				std::vector<ASTNode> additional_types = rewriteSequence(
					node.additional_type_nodes(), Role::TypeArgument, zero_to_many);
				return wrap<OneToOne, ZeroToMany>(ExpressionNode(TypeTraitExprNode(
					node.kind(), type, std::move(additional_types), node.trait_token())));
			}
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(TypeTraitExprNode(
				node.kind(), type, node.trait_token())));
		} else if constexpr (std::is_same_v<T, NewExpressionNode>) {
			TypeSpecifierNode type = rewriteType(node.type_node(), Role::ConstructedType, one_to_one);
			std::optional<ASTNode> size;
			if (node.size_expr().has_value()) {
				size = rewriteOne(*node.size_expr(), Role::ArrayBound, one_to_one);
			}
			std::vector<ASTNode> constructor_arguments = rewriteSequence(
				node.constructor_args(), Role::ConstructorArgument, zero_to_many);
			std::vector<ASTNode> placement_arguments = rewriteSequence(
				node.placement_args(), Role::PlacementArgument, zero_to_many);
			ChunkedVector<ASTNode, 128, 256> stored_constructor_arguments;
			for (const ASTNode& argument : constructor_arguments) {
				stored_constructor_arguments.push_back(argument);
			}
			TemplateVector<ASTNode, 2> stored_placement_arguments;
			for (const ASTNode& argument : placement_arguments) {
				stored_placement_arguments.push_back(argument);
			}
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(NewExpressionNode(
				ASTNode::emplace_node<TypeSpecifierNode>(type),
				node.is_array(),
				std::move(size),
				std::move(stored_constructor_arguments),
				std::move(stored_placement_arguments),
				node.has_value_init(),
				node.is_brace_init())));
		} else if constexpr (std::is_same_v<T, DeleteExpressionNode>) {
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(DeleteExpressionNode(
				rewriteOne(node.expr(), Role::Operand, one_to_one),
				node.is_array())));
		} else if constexpr (std::is_same_v<T, StaticCastNode> ||
					  std::is_same_v<T, DynamicCastNode> ||
					  std::is_same_v<T, ConstCastNode> ||
					  std::is_same_v<T, ReinterpretCastNode>) {
			TypeSpecifierNode target = rewriteType(node.target_type(), Role::CastTarget, one_to_one);
			ASTNode operand = rewriteOne(node.expr(), Role::Operand, one_to_one);
			if constexpr (std::is_same_v<T, StaticCastNode>) {
				return wrap<OneToOne, ZeroToMany>(ExpressionNode(StaticCastNode(target, operand, node.cast_token())));
			} else if constexpr (std::is_same_v<T, DynamicCastNode>) {
				return wrap<OneToOne, ZeroToMany>(ExpressionNode(DynamicCastNode(target, operand, node.cast_token())));
			} else if constexpr (std::is_same_v<T, ConstCastNode>) {
				return wrap<OneToOne, ZeroToMany>(ExpressionNode(ConstCastNode(target, operand, node.cast_token())));
			} else {
				return wrap<OneToOne, ZeroToMany>(ExpressionNode(ReinterpretCastNode(target, operand, node.cast_token())));
			}
		} else if constexpr (std::is_same_v<T, TypeidNode>) {
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(TypeidNode(
				rewriteOne(node.operand(), node.is_type() ? Role::TypeOperand : Role::Operand, one_to_one),
				node.is_type(),
				node.typeid_token())));
		} else if constexpr (std::is_same_v<T, LambdaExpressionNode>) {
			std::vector<LambdaCaptureNode> captures;
			for (const LambdaCaptureNode& capture : node.captures()) {
				std::optional<ASTNode> initializer;
				if (capture.has_initializer()) {
					initializer = rewriteOne(*capture.initializer(), Role::CaptureInitializer, one_to_one);
				}
				captures.emplace_back(capture.kind(), capture.identifier_token(), std::move(initializer));
			}
			std::vector<ASTNode> parameters;
			for (const ASTNode& parameter : node.parameters()) {
				parameters.push_back(rewriteOne(parameter, Role::LambdaParameter, one_to_one));
			}
			std::optional<ASTNode> return_type;
			if (node.return_type().has_value()) {
				return_type = rewriteOne(*node.return_type(), Role::LambdaReturnType, one_to_one);
			}
			LambdaExpressionNode rewritten(
				std::move(captures),
				std::move(parameters),
				rewriteOne(node.body(), Role::Body, one_to_one),
				std::move(return_type),
				node.lambda_token(),
				node.is_mutable(),
				std::vector<std::string_view>(node.template_params().begin(), node.template_params().end()),
				node.is_noexcept(),
				node.is_constexpr(),
				node.is_consteval());
			if (node.outer_template_environment_snapshot() != nullptr) {
				rewritten.set_outer_template_bindings(node.outer_template_environment_snapshot());
			} else if (!node.outer_template_param_names().empty()) {
				rewritten.set_outer_template_bindings(node.outer_template_param_names(), node.outer_template_args());
			}
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(std::move(rewritten)));
		} else if constexpr (std::is_same_v<T, FoldExpressionNode>) {
			ASTNode pack_expression;
			if (node.pack_expr().has_value()) {
				pack_expression = rewriteOne(*node.pack_expr(), Role::FoldPackExpression, one_to_one);
			}
			std::optional<ASTNode> init;
			if (node.init_expr().has_value()) {
				init = rewriteOne(*node.init_expr(), Role::FoldInitializer, one_to_one);
			}
			FoldExpressionNode rewritten = [&]() {
				if (node.pack_expr().has_value()) {
					if (init.has_value()) {
						return FoldExpressionNode(pack_expression, node.op(), node.direction(), *init, node.get_token());
					}
					return FoldExpressionNode(pack_expression, node.op(), node.direction(), node.get_token());
				}
				if (init.has_value()) {
					return FoldExpressionNode(node.pack_name(), node.op(), node.direction(), *init, node.get_token());
				}
				return FoldExpressionNode(node.pack_name(), node.op(), node.direction(), node.get_token());
			}();
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(std::move(rewritten)));
		} else if constexpr (std::is_same_v<T, PackExpansionExprNode>) {
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(PackExpansionExprNode(
				rewriteOne(node.pattern(), Role::PackPattern, one_to_one),
				node.get_token())));
		} else if constexpr (std::is_same_v<T, PseudoDestructorCallNode>) {
			if (node.has_qualified_name()) {
				return wrap<OneToOne, ZeroToMany>(ExpressionNode(PseudoDestructorCallNode(
					rewriteOne(node.object(), Role::Receiver, one_to_one),
					StringTable::getStringView(node.qualified_type_name()),
					node.type_name_token(),
					node.is_arrow_access())));
			}
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(PseudoDestructorCallNode(
				rewriteOne(node.object(), Role::Receiver, one_to_one),
				node.type_name_token(),
				node.is_arrow_access())));
		} else if constexpr (std::is_same_v<T, NoexceptExprNode>) {
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(NoexceptExprNode(
				rewriteOne(node.expr(), Role::Operand, one_to_one),
				node.noexcept_token())));
		} else if constexpr (std::is_same_v<T, InitializerListConstructionNode>) {
			std::vector<ASTNode> elements;
			for (const ASTNode& element : node.elements()) {
				elements.push_back(rewriteOne(element, Role::InitializerElement, one_to_one));
			}
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(InitializerListConstructionNode(
				rewriteOne(node.element_type(), Role::InitializerElementType, one_to_one),
				rewriteOne(node.target_type(), Role::InitializerTargetType, one_to_one),
				std::move(elements),
				node.called_from())));
		} else if constexpr (std::is_same_v<T, ThrowExpressionNode>) {
			if (node.expression().has_value()) {
				return wrap<OneToOne, ZeroToMany>(ExpressionNode(ThrowExpressionNode(
					rewriteOne(*node.expression(), Role::Operand, one_to_one),
					node.throw_token())));
			}
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(ThrowExpressionNode(node.throw_token())));
		} else if constexpr (std::is_same_v<T, CallExprNode>) {
			std::optional<ASTNode> receiver;
			if (node.has_receiver()) {
				receiver = rewriteOne(node.receiver(), Role::Receiver, one_to_one);
			}
			std::vector<ASTNode> arguments = rewriteSequence(
				node.arguments(), Role::CallArgument, zero_to_many);
			ChunkedVector<ASTNode> stored_arguments;
			for (const ASTNode& argument : arguments) {
				stored_arguments.push_back(argument);
			}
			CallExprNode rewritten = receiver.has_value()
				? CallExprNode(node.callee(), *receiver, std::move(stored_arguments), node.called_from())
				: CallExprNode(node.callee(), std::move(stored_arguments), node.called_from());
			CallMetadataCopyOptions options{};
			copyCallMetadataWithTransformedTemplateArguments(
				rewritten,
				node,
				[&](const ASTNode& argument) {
					return rewriteOne(argument, Role::TemplateArgument, one_to_one);
				},
				options);
			return wrap<OneToOne, ZeroToMany>(ExpressionNode(std::move(rewritten)));
		} else {
			static_assert(
				ExpressionRewriterDetail::dependent_false_v<T>,
				"Every ExpressionNode alternative must be reconstructed by ExpressionRewriter");
		}
	}
};
