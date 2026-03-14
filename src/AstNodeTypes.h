#pragma once

#include "AstNodeTypes_Core.h"
#include "AstNodeTypes_TypeSystem.h"
#include "AstNodeTypes_DeclNodes.h"
#include "AstNodeTypes_Template.h"
#include "AstNodeTypes_Expr.h"
#include "AstNodeTypes_Stmt.h"

inline std::string_view getIdentifierNameFromAstNode(const ASTNode& node) {
	if (node.is<ExpressionNode>()) {
		const ExpressionNode& expr_node = node.as<ExpressionNode>();
		if (std::holds_alternative<IdentifierNode>(expr_node)) {
			return std::get<IdentifierNode>(expr_node).name();
		}
		return {};
	}
	if (node.is<IdentifierNode>()) {
		return node.as<IdentifierNode>().name();
	}
	return {};
}

// Extract a QualifiedIdentifierNode from a normalized expression result node.
// Parser expression paths return QualifiedIdentifierNode wrapped in ExpressionNode.
inline const QualifiedIdentifierNode& asQualifiedIdentifier(const ASTNode& node) {
	return std::get<QualifiedIdentifierNode>(node.as<ExpressionNode>());
}
