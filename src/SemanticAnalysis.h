#pragma once
#include "SemanticTypes.h"
#include "AstNodeTypes_Core.h"

class Parser;
class CompileContext;
class SymbolTable;
class StructDeclarationNode;
class FunctionDeclarationNode;
class BlockNode;
class NamespaceDeclarationNode;

// --- Semantic analysis pass ---
// Post-parse semantic normalization. Phase 1 is a no-op traversal that
// establishes the pipeline seam, collects stats, and validates that the
// pass can walk the full AST without errors.

class SemanticAnalysis {
public:
	SemanticAnalysis(Parser& parser, CompileContext& context, SymbolTable& symbols);

	// Main entry point: run the translation-unit semantic pass.
	void run();

	// Access collected statistics after run().
	const SemanticPassStats& stats() const { return stats_; }

	// Access the type interning context.
	const TypeContext& typeContext() const { return type_context_; }

	// Access cast info side table.
	const std::vector<ImplicitCastInfo>& castInfoTable() const { return cast_info_table_; }

private:
	// Top-level dispatch
	void normalizeTopLevelNode(ASTNode node);

	// Declaration handlers
	void normalizeFunctionDeclaration(const FunctionDeclarationNode& func);
	void normalizeStructDeclaration(const StructDeclarationNode& decl);
	void normalizeNamespace(const NamespaceDeclarationNode& ns);

	// Statement handler
	void normalizeStatement(ASTNode node, const SemanticContext& ctx);
	void normalizeBlock(const BlockNode& block, const SemanticContext& ctx);

	// Expression handler (Phase 1: just counts, no actual normalization)
	SemanticExprInfo normalizeExpression(ASTNode node, const SemanticContext& ctx);

	// Helpers
	CanonicalTypeId canonicalizeType(const TypeSpecifierNode& type);

	// State
	Parser& parser_;
	CompileContext& context_;
	SymbolTable& symbols_;
	TypeContext type_context_;
	std::vector<ImplicitCastInfo> cast_info_table_;
	SemanticPassStats stats_;
};
