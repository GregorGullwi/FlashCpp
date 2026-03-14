#pragma once

#include "AstNodeTypes_Core.h"
#include "AstNodeTypes_TypeSystem.h"
#include "AstNodeTypes_DeclNodes.h"
#include "AstNodeTypes_Template.h"
#include "AstNodeTypes_Expr.h"
#include "AstNodeTypes_Stmt.h"

// Parser-produced expression results now prefer expression leaf nodes wrapped in
// ExpressionNode, but older/synthetic ASTs may still store them directly.
// This template centralizes tolerant extraction from both forms.
template <typename T>
inline const T* tryGetNode(const ASTNode& node) {
	if (node.is<ExpressionNode>()) {
		return std::get_if<T>(&node.as<ExpressionNode>());
	}
	if (node.is<T>()) {
		return &node.as<T>();
	}
	return nullptr;
}

inline const IdentifierNode* tryGetIdentifier(const ASTNode& node) {
	return tryGetNode<IdentifierNode>(node);
}

inline const QualifiedIdentifierNode* tryGetQualifiedIdentifier(const ASTNode& node) {
	return tryGetNode<QualifiedIdentifierNode>(node);
}

inline std::string_view getIdentifierNameFromAstNode(const ASTNode& node) {
	if (const IdentifierNode* identifier = tryGetIdentifier(node)) {
		return identifier->name();
	}
	return {};
}

// Extract a QualifiedIdentifierNode from a normalized expression result node.
// Parser expression paths return QualifiedIdentifierNode wrapped in ExpressionNode.
inline const QualifiedIdentifierNode& asQualifiedIdentifier(const ASTNode& node) {
	return std::get<QualifiedIdentifierNode>(node.as<ExpressionNode>());
}
