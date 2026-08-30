#pragma once

#include <type_traits>
#include <vector>

#include "ChunkedAnyVector.h"

// Forward declarations only. Leaf and syntax types permitted in gChunkedAnyStorage
// must be listed explicitly in isLegacyChunkedAnyStorageTypeFor below.
// ExpressionNode (the variant wrapper) is admitted in LegacyChunkedAnyStoragePolicyComplete.h
// after AstNodeTypes_Expr.h is available.

struct FunctionCallableTypes;
struct TemplateEnvironmentSnapshotNode;
struct TemplateTypeArg;

class AlignofExprNode;
class ArraySubscriptNode;
class BinaryOperatorNode;
class BlockNode;
class BoolLiteralNode;
class BreakStatementNode;
class CallExprNode;
class CaseLabelNode;
class CatchClauseNode;
class CompoundRequirementNode;
class ConceptDeclarationNode;
class ConstructorCallNode;
class ConstructorDeclarationNode;
class ConstCastNode;
class ContinueStatementNode;
class DeclarationNode;
class DeductionGuideNode;
class DefaultLabelNode;
class DeleteExpressionNode;
class DestructorDeclarationNode;
class DoWhileStatementNode;
class DynamicCastNode;
class EnumDeclarationNode;
class EnumeratorNode;
class FoldExpressionNode;
class ForStatementNode;
class FriendDeclarationNode;
class FunctionDeclarationNode;
class GotoStatementNode;
class IdentifierNode;
class IfStatementNode;
class InitializerListConstructionNode;
class InitializerListNode;
class LabelStatementNode;
class LambdaExpressionNode;
class MemberAccessNode;
class NamespaceAliasNode;
class NamespaceDeclarationNode;
class NewExpressionNode;
class NoexceptExprNode;
class NumericLiteralNode;
class OffsetofExprNode;
class PackExpansionExprNode;
class PointerToMemberAccessNode;
class PseudoDestructorCallNode;
class QualifiedIdentifierNode;
class RangedForStatementNode;
class ReinterpretCastNode;
class RequiresClauseNode;
class RequiresExpressionNode;
class ReturnStatementNode;
class SehExceptClauseNode;
class SehFilterExpressionNode;
class SehFinallyClauseNode;
class SehLeaveStatementNode;
class SehTryExceptStatementNode;
class SehTryFinallyStatementNode;
class SizeofExprNode;
class SizeofPackNode;
class StaticCastNode;
class StringLiteralNode;
class StructDeclarationNode;
class StructuredBindingNode;
class SwitchStatementNode;
class TemplateAliasNode;
class TemplateClassDeclarationNode;
class TemplateFunctionDeclarationNode;
class TemplateParameterNode;
class TemplateParameterReferenceNode;
class TemplateVariableDeclarationNode;
class TernaryOperatorNode;
class ThrowExpressionNode;
class ThrowStatementNode;
class TryStatementNode;
class TypedefDeclarationNode;
class TypeidNode;
class TypeSpecifierNode;
class TypeTraitExprNode;
class UnaryOperatorNode;
class UsingDeclarationNode;
class UsingDirectiveNode;
class UsingEnumNode;
class VariableDeclarationNode;
class WhileStatementNode;

namespace detail {

template<typename T>
struct IsTemplateTypeArgVector : std::false_type {};

template<typename Alloc>
struct IsTemplateTypeArgVector<std::vector<TemplateTypeArg, Alloc>> : std::true_type {};

template<typename T>
constexpr bool isLegacyChunkedAnyStorageTypeFor =
	std::is_same_v<std::decay_t<T>, IdentifierNode> ||
	std::is_same_v<std::decay_t<T>, QualifiedIdentifierNode> ||
	std::is_same_v<std::decay_t<T>, StringLiteralNode> ||
	std::is_same_v<std::decay_t<T>, NumericLiteralNode> ||
	std::is_same_v<std::decay_t<T>, BoolLiteralNode> ||
	std::is_same_v<std::decay_t<T>, BinaryOperatorNode> ||
	std::is_same_v<std::decay_t<T>, UnaryOperatorNode> ||
	std::is_same_v<std::decay_t<T>, TernaryOperatorNode> ||
	std::is_same_v<std::decay_t<T>, ConstructorCallNode> ||
	std::is_same_v<std::decay_t<T>, MemberAccessNode> ||
	std::is_same_v<std::decay_t<T>, PointerToMemberAccessNode> ||
	std::is_same_v<std::decay_t<T>, ArraySubscriptNode> ||
	std::is_same_v<std::decay_t<T>, SizeofExprNode> ||
	std::is_same_v<std::decay_t<T>, SizeofPackNode> ||
	std::is_same_v<std::decay_t<T>, AlignofExprNode> ||
	std::is_same_v<std::decay_t<T>, OffsetofExprNode> ||
	std::is_same_v<std::decay_t<T>, TypeTraitExprNode> ||
	std::is_same_v<std::decay_t<T>, NewExpressionNode> ||
	std::is_same_v<std::decay_t<T>, DeleteExpressionNode> ||
	std::is_same_v<std::decay_t<T>, StaticCastNode> ||
	std::is_same_v<std::decay_t<T>, DynamicCastNode> ||
	std::is_same_v<std::decay_t<T>, ConstCastNode> ||
	std::is_same_v<std::decay_t<T>, ReinterpretCastNode> ||
	std::is_same_v<std::decay_t<T>, TypeidNode> ||
	std::is_same_v<std::decay_t<T>, LambdaExpressionNode> ||
	std::is_same_v<std::decay_t<T>, TemplateParameterReferenceNode> ||
	std::is_same_v<std::decay_t<T>, FoldExpressionNode> ||
	std::is_same_v<std::decay_t<T>, PackExpansionExprNode> ||
	std::is_same_v<std::decay_t<T>, PseudoDestructorCallNode> ||
	std::is_same_v<std::decay_t<T>, NoexceptExprNode> ||
	std::is_same_v<std::decay_t<T>, InitializerListConstructionNode> ||
	std::is_same_v<std::decay_t<T>, ThrowExpressionNode> ||
	std::is_same_v<std::decay_t<T>, CallExprNode> ||
	std::is_same_v<std::decay_t<T>, BlockNode> ||
	std::is_same_v<std::decay_t<T>, BreakStatementNode> ||
	std::is_same_v<std::decay_t<T>, CaseLabelNode> ||
	std::is_same_v<std::decay_t<T>, CatchClauseNode> ||
	std::is_same_v<std::decay_t<T>, CompoundRequirementNode> ||
	std::is_same_v<std::decay_t<T>, ConceptDeclarationNode> ||
	std::is_same_v<std::decay_t<T>, ConstructorDeclarationNode> ||
	std::is_same_v<std::decay_t<T>, ContinueStatementNode> ||
	std::is_same_v<std::decay_t<T>, DeclarationNode> ||
	std::is_same_v<std::decay_t<T>, DeductionGuideNode> ||
	std::is_same_v<std::decay_t<T>, DefaultLabelNode> ||
	std::is_same_v<std::decay_t<T>, DestructorDeclarationNode> ||
	std::is_same_v<std::decay_t<T>, DoWhileStatementNode> ||
	std::is_same_v<std::decay_t<T>, EnumDeclarationNode> ||
	std::is_same_v<std::decay_t<T>, EnumeratorNode> ||
	std::is_same_v<std::decay_t<T>, ForStatementNode> ||
	std::is_same_v<std::decay_t<T>, FriendDeclarationNode> ||
	std::is_same_v<std::decay_t<T>, FunctionCallableTypes> ||
	std::is_same_v<std::decay_t<T>, FunctionDeclarationNode> ||
	std::is_same_v<std::decay_t<T>, GotoStatementNode> ||
	std::is_same_v<std::decay_t<T>, IfStatementNode> ||
	std::is_same_v<std::decay_t<T>, InitializerListNode> ||
	std::is_same_v<std::decay_t<T>, LabelStatementNode> ||
	std::is_same_v<std::decay_t<T>, NamespaceAliasNode> ||
	std::is_same_v<std::decay_t<T>, NamespaceDeclarationNode> ||
	std::is_same_v<std::decay_t<T>, RangedForStatementNode> ||
	std::is_same_v<std::decay_t<T>, RequiresClauseNode> ||
	std::is_same_v<std::decay_t<T>, RequiresExpressionNode> ||
	std::is_same_v<std::decay_t<T>, ReturnStatementNode> ||
	std::is_same_v<std::decay_t<T>, SehExceptClauseNode> ||
	std::is_same_v<std::decay_t<T>, SehFilterExpressionNode> ||
	std::is_same_v<std::decay_t<T>, SehFinallyClauseNode> ||
	std::is_same_v<std::decay_t<T>, SehLeaveStatementNode> ||
	std::is_same_v<std::decay_t<T>, SehTryExceptStatementNode> ||
	std::is_same_v<std::decay_t<T>, SehTryFinallyStatementNode> ||
	std::is_same_v<std::decay_t<T>, StructDeclarationNode> ||
	std::is_same_v<std::decay_t<T>, StructuredBindingNode> ||
	std::is_same_v<std::decay_t<T>, SwitchStatementNode> ||
	std::is_same_v<std::decay_t<T>, TemplateAliasNode> ||
	std::is_same_v<std::decay_t<T>, TemplateClassDeclarationNode> ||
	std::is_same_v<std::decay_t<T>, TemplateEnvironmentSnapshotNode> ||
	std::is_same_v<std::decay_t<T>, TemplateFunctionDeclarationNode> ||
	std::is_same_v<std::decay_t<T>, TemplateParameterNode> ||
	std::is_same_v<std::decay_t<T>, TemplateVariableDeclarationNode> ||
	std::is_same_v<std::decay_t<T>, ThrowStatementNode> ||
	std::is_same_v<std::decay_t<T>, TryStatementNode> ||
	std::is_same_v<std::decay_t<T>, TypedefDeclarationNode> ||
	std::is_same_v<std::decay_t<T>, TypeSpecifierNode> ||
	std::is_same_v<std::decay_t<T>, UsingDeclarationNode> ||
	std::is_same_v<std::decay_t<T>, UsingDirectiveNode> ||
	std::is_same_v<std::decay_t<T>, UsingEnumNode> ||
	std::is_same_v<std::decay_t<T>, VariableDeclarationNode> ||
	std::is_same_v<std::decay_t<T>, WhileStatementNode> ||
	IsTemplateTypeArgVector<std::decay_t<T>>::value;

} // namespace detail
