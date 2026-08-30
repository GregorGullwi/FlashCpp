#pragma once

#include <cstddef>
#include <type_traits>
#include <vector>

#include "AstNodeTypes_Expr.h"
#include "AstNodeTypes_Stmt.h"
#include "AstNodeTypes_TypeSystem.h"
#include "TemplateEnvironment.h"
#include "TemplateRegistry_Types.h"

// Compile-time allow-list for types that may be allocated in gChunkedAnyStorage.
// New semantic objects must use FrontendContext typed arenas instead.
// Partially specializes LegacyChunkedAnyStorageTraits<T, true> from ChunkedAnyVector.h.

namespace detail {

template<typename T, std::size_t... Indices>
constexpr bool isExpressionNodeAlternativeImpl(std::index_sequence<Indices...>) {
	return (std::is_same_v<std::decay_t<T>, std::variant_alternative_t<Indices, ExpressionNode>> || ...);
}

template<typename T>
constexpr bool isExpressionNodeAlternative =
	isExpressionNodeAlternativeImpl<std::decay_t<T>>(std::make_index_sequence<std::variant_size_v<ExpressionNode>>{});

template<typename T>
struct IsTemplateTypeArgVector : std::false_type {};

template<typename Alloc>
struct IsTemplateTypeArgVector<std::vector<TemplateTypeArg, Alloc>> : std::true_type {};

template<typename T>
constexpr bool isLegacySyntaxOrAuxAstNode =
	std::is_same_v<std::decay_t<T>, ExpressionNode> ||
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

template<typename T>
inline constexpr bool isLegacyChunkedAnyStorageType =
	detail::isExpressionNodeAlternative<std::decay_t<T>> || detail::isLegacySyntaxOrAuxAstNode<std::decay_t<T>>;

template<typename T>
struct LegacyChunkedAnyStorageTraits<T, true> {
	static constexpr bool allowed = isLegacyChunkedAnyStorageType<std::decay_t<T>>;
};
