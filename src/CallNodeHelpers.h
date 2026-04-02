#pragma once

#include "AstNodeTypes_DeclNodes.h"
#include "AstNodeTypes_Expr.h"

// ============================================================================
// CallInfo — a lightweight read-only view over any call-expression node.
//
// Provides a uniform interface for extracting call metadata from
// FunctionCallNode, MemberFunctionCallNode, or the new CallExprNode.
// Downstream code can construct a CallInfo and then inspect fields without
// branching on the concrete node type.
//
// The view does NOT own the underlying data; it must not outlive the
// source node.
// ============================================================================

struct CallInfo {
	// Callee declaration (always present).
	const DeclarationNode* declaration;

	// Full function declaration (null when only a DeclarationNode is available).
	const FunctionDeclarationNode* function_declaration;

	// Arguments (always present).
	const ChunkedVector<ASTNode>* arguments;

	// Source location token.
	Token called_from;

	// Receiver object (empty ASTNode when there is no receiver).
	ASTNode receiver;
	bool has_receiver;

	// Optional metadata — empty/invalid when the source node does not carry them.
	StringHandle mangled_name;
	StringHandle qualified_name;
	const std::vector<ASTNode>* template_arguments;  // null when absent
	bool is_indirect;

	// --- Factory helpers ---------------------------------------------------

	static CallInfo from(const FunctionCallNode& node) {
		CallInfo info;
		info.declaration           = &node.function_declaration();
		info.function_declaration  = nullptr;
		info.arguments             = &node.arguments();
		info.called_from           = node.called_from();
		info.receiver              = ASTNode();
		info.has_receiver          = false;
		info.mangled_name          = node.mangled_name_handle();
		info.qualified_name        = node.qualified_name_handle();
		info.template_arguments    = &node.template_arguments();
		info.is_indirect           = node.is_indirect_call();
		return info;
	}

	static CallInfo from(const MemberFunctionCallNode& node) {
		CallInfo info;
		info.declaration           = &node.function_declaration().decl_node();
		info.function_declaration  = &node.function_declaration();
		info.arguments             = &node.arguments();
		info.called_from           = node.called_from();
		info.receiver              = node.object();
		info.has_receiver          = true;
		info.mangled_name          = StringHandle();
		info.qualified_name        = StringHandle();
		info.template_arguments    = nullptr;
		info.is_indirect           = false;
		return info;
	}

	static CallInfo from(const CallExprNode& node) {
		CallInfo info;
		info.declaration           = &node.callee().declaration();
		info.function_declaration  = node.callee().function_declaration_or_null();
		info.arguments             = &node.arguments();
		info.called_from           = node.called_from();
		info.receiver              = node.has_receiver() ? node.receiver() : ASTNode();
		info.has_receiver          = node.has_receiver();
		info.mangled_name          = node.mangled_name_handle();
		info.qualified_name        = node.qualified_name_handle();
		info.template_arguments    = &node.template_arguments();
		info.is_indirect           = node.callee().is_indirect();
		return info;
	}

	// Build a CallInfo from an ExpressionNode that holds one of the three
	// call-node alternatives.  Returns std::nullopt for any other alternative.
	static std::optional<CallInfo> tryFrom(const ExpressionNode& expr) {
		if (auto* p = std::get_if<FunctionCallNode>(&expr))
			return from(*p);
		if (auto* p = std::get_if<MemberFunctionCallNode>(&expr))
			return from(*p);
		if (auto* p = std::get_if<CallExprNode>(&expr))
			return from(*p);
		return std::nullopt;
	}
};
