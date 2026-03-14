#pragma once

#include "AstNodeTypes_Core.h"
#include "AstNodeTypes_TypeSystem.h"
#include "AstNodeTypes_DeclNodes.h"
#include "AstNodeTypes_Template.h"
#include "AstNodeTypes_Expr.h"
#include "AstNodeTypes_Stmt.h"

// Parser-produced expression results now prefer IdentifierNode wrapped in
// ExpressionNode, but older/synthetic ASTs may still store IdentifierNode
// directly. Keep this helper tolerant of both forms for downstream consumers.
inline const IdentifierNode* tryGetIdentifier(const ASTNode& node) {
	if (node.is<ExpressionNode>()) {
		const ExpressionNode& expr_node = node.as<ExpressionNode>();
		if (std::holds_alternative<IdentifierNode>(expr_node)) {
			return &std::get<IdentifierNode>(expr_node);
		}
		return nullptr;
	}
	if (node.is<IdentifierNode>()) {
		return &node.as<IdentifierNode>();
	}
	return nullptr;
}

// Parser-produced expression results now prefer QualifiedIdentifierNode wrapped
// in ExpressionNode, but older/synthetic ASTs may still store
// QualifiedIdentifierNode directly. Keep this helper tolerant of both forms for
// downstream consumers.
inline const QualifiedIdentifierNode* tryGetQualifiedIdentifier(const ASTNode& node) {
	if (node.is<ExpressionNode>()) {
		const ExpressionNode& expr_node = node.as<ExpressionNode>();
		if (std::holds_alternative<QualifiedIdentifierNode>(expr_node)) {
			return &std::get<QualifiedIdentifierNode>(expr_node);
		}
		return nullptr;
	}
	if (node.is<QualifiedIdentifierNode>()) {
		return &node.as<QualifiedIdentifierNode>();
	}
	return nullptr;
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
