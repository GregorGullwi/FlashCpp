#pragma once
#include "SemanticTypes.h"
#include "AstNodeTypes_Core.h"
#include <optional>
#include <unordered_map>
#include <string_view>
#include <vector>

class Parser;
class CompileContext;
class SymbolTable;
class StructDeclarationNode;
class FunctionDeclarationNode;
class BlockNode;
class NamespaceDeclarationNode;
class BinaryOperatorNode;
class FunctionCallNode;
class RangedForStatementNode;
class VariableDeclarationNode;
struct LambdaInfo;

// --- Semantic analysis pass ---
// Post-parse semantic normalization. Phase 1 established the pipeline seam.
// Phase 2 migrates standard return-value conversions into the semantic pass.

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

	// Look up the semantic slot for an expression node.
	// Key is the raw pointer to the ExpressionNode (stable, from gChunkedAnyStorage).
	std::optional<SemanticSlot> getSlot(const void* key) const;

	// Instantiation-time semantic hook for generic lambda parameter normalization.
	std::vector<ASTNode> normalizeGenericLambdaParams(
		const std::vector<ASTNode>& parameter_nodes,
		const std::vector<std::pair<size_t, TypeSpecifierNode>>& deduced_types) const;
	void normalizeInstantiatedLambdaBody(LambdaInfo& lambda_info);
	ASTNode normalizeRangedForLoopDecl(const VariableDeclarationNode& original_var_decl,
		const TypeSpecifierNode& deduced_type) const;
	ASTNode normalizeRangedForLoopDecl(const VariableDeclarationNode& original_var_decl,
		const TypeSpecifierNode& range_type,
		const TypeSpecifierNode& begin_return_type,
		bool prefer_const_deref) const;
	ASTNode normalizeRangedForLoopDecl(const RangedForStatementNode& stmt) const;

private:
	// Top-level dispatch
	void normalizeTopLevelNode(const ASTNode& node);

	// Declaration handlers
	void normalizeFunctionDeclaration(const FunctionDeclarationNode& func);
	void normalizeStructDeclaration(const StructDeclarationNode& decl);
	void normalizeNamespace(const NamespaceDeclarationNode& ns);

	// Statement handler
	void normalizeStatement(const ASTNode& node, const SemanticContext& ctx);
	void normalizeBlock(const BlockNode& block, const SemanticContext& ctx);

	// Expression handler (counts and infers types for annotatable expressions)
	SemanticExprInfo normalizeExpression(const ASTNode& node, const SemanticContext& ctx);

	// Helpers
	CanonicalTypeId canonicalizeType(const TypeSpecifierNode& type);
	void resolveRemainingAutoReturns();
	void resolveRemainingAutoReturnsInNode(ASTNode& node);
	std::optional<TypeSpecifierNode> deducePlaceholderReturnType(const ASTNode& body, Type placeholder_type);
	TypeSpecifierNode finalizePlaceholderDeduction(Type placeholder_type, const TypeSpecifierNode& deduced_type) const;

	// Infer the canonical type of a simple expression without full evaluation.
	// Handles: NumericLiteralNode, BoolLiteralNode, IdentifierNode (via scope stack).
	// Returns invalid CanonicalTypeId if inference is not possible.
	CanonicalTypeId inferExpressionType(const ASTNode& node);

	// Allocate a new ImplicitCastInfo entry and return its 1-based index.
	CastInfoIndex allocateCastInfo(const ImplicitCastInfo& info);

	// Store a semantic slot for the given expression node pointer.
	void setSlot(const void* key, const SemanticSlot& slot);

	// Core annotation helper: if the inferred type of expr_node differs from target_type_id
	// and a standard (non-user-defined) primitive conversion applies, allocate an
	// ImplicitCastInfo and fill the expression's SemanticSlot.
	// Returns true when a slot was filled.
	bool tryAnnotateConversion(const ASTNode& expr_node, CanonicalTypeId target_type_id);

	// Try to annotate a return expression with implicit cast info when the
	// expression type differs from the declared function return type.
	void tryAnnotateReturnConversion(const ASTNode& expr_node, const SemanticContext& ctx);

	// Annotate binary arithmetic/comparison operands with their common-type conversions.
	void tryAnnotateBinaryOperandConversions(const BinaryOperatorNode& bin_op);

	// Annotate function-call arguments with their parameter-type conversions.
	void tryAnnotateCallArgConversions(const FunctionCallNode& call_node);

	// Scope stack for local variable type tracking (used by inferExpressionType).
	// Keys are StringHandles from the string pool (stable for the compilation lifetime).
	void pushScope();
	void popScope();
	void addLocalType(StringHandle name, CanonicalTypeId type_id);
	CanonicalTypeId lookupLocalType(StringHandle name) const;

	// State
	Parser& parser_;
	CompileContext& context_;
	SymbolTable& symbols_;
	TypeContext type_context_;
	std::vector<ImplicitCastInfo> cast_info_table_;
	SemanticPassStats stats_;

	// Side table: expression node pointer → semantic slot.
	std::unordered_map<const void*, SemanticSlot> semantic_slots_;

	// Scope stack: each entry maps local variable StringHandle → canonical type id.
	std::vector<std::unordered_map<StringHandle, CanonicalTypeId>> scope_stack_;
};
