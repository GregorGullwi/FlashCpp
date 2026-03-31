#pragma once
#include "SemanticTypes.h"
#include "AstNodeTypes_Core.h"
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <string_view>
#include <vector>

class Parser;
class CompileContext;
class SymbolTable;
class StructDeclarationNode;
class FunctionDeclarationNode;
class ConstructorDeclarationNode;
class DestructorDeclarationNode;
class BlockNode;
class NamespaceDeclarationNode;
class BinaryOperatorNode;
class UnaryOperatorNode;
class FunctionCallNode;
class ConstructorCallNode;
class InitializerListNode;
class RangedForStatementNode;
class VariableDeclarationNode;
struct StructTypeInfo;
struct LambdaInfo;

// --- Semantic analysis pass ---
// Post-parse semantic normalization.  See docs/IMPLICIT_CAST_SEMA_PLAN.md for
// the full phase history.  Summary of completed phases:
//   Phase 1: Pipeline seam (AST walk, auto-return deduction)
//   Phase 2: Return-value conversions
//   Phase 3: Binary operand conversions (usual arithmetic conversions)
//   Phase 4: Shift operand promotions (independent integral promotions)
//   Phase 5: Contextual bool for primitives (if/while/for/ternary/&&/||)
//   Phase 6: Function call argument conversions + callable operator() resolution
//   Phase 7: Ternary branch conversions, assignment RHS, variable initialiser
//   Phase 8: Constructor arg conversions, enum/pointer contextual bool, float→int folding
//   Phase 9-12: Global/static assignment, sema target verification, buildConversionPlan,
//               enum→primitive annotations, identifier type inference fallback
//   Phase 13: Unary operator integral promotions, scoped enum diagnostic,
//             inferExpressionType expansion (~/!/++/--/sizeof/alignof)

class SemanticAnalysis {
public:
	SemanticAnalysis(Parser& parser, CompileContext& context, SymbolTable& symbols);

 // Main entry point: run the translation-unit semantic pass.
 // Phase 1 boundary guard: starts with a lightweight walk over the sema-owned
 // post-parse AST surface and reports forbidden surviving template-only
 // expression nodes before semantic normalization begins.
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

 // Returns true if sema normalized the function body identified by its ASTNode pointer.
 // Codegen uses this to skip Phase 15 warnings for bodies sema never visited.
	bool hasNormalizedBody(const void* body_ptr) const {
		return normalized_bodies_.count(body_ptr) > 0;
	}

 // Returns true if sema attempted to annotate this FunctionCallNode but could not
 // resolve the callee (e.g. template specialization with separate DeclarationNode copies).
 // Codegen uses this to suppress Phase 15 hard enforcement for known unresolvable cases.
	bool hasUnresolvedCallArgs(const FunctionCallNode* call) const {
		return unresolved_call_args_.count(call) > 0;
	}

 // Look up the compound assignment back-conversion slot (keyed by BinaryOperatorNode address).
 // Returns non-empty when sema annotated a commonType→lhsType result back-conversion.
	std::optional<SemanticSlot> getCompoundAssignBackConv(const void* binop_key) const {
		auto it = compound_assign_back_conv_.find(binop_key);
		if (it == compound_assign_back_conv_.end())
			return {};
		return it->second;
	}

 // Look up the pre-resolved callable operator() for a FunctionCallNode.
 // Returns nullptr when no annotation was stored (non-callable or not yet resolved).
	const FunctionDeclarationNode* getResolvedOpCall(const FunctionCallNode* key) const;
	const CallArgReferenceBindingInfo* getFunctionCallRefBinding(const FunctionCallNode* key, size_t arg_index) const;
	const CallArgReferenceBindingInfo* getMemberFunctionCallRefBinding(const MemberFunctionCallNode* key, size_t arg_index) const;

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
									   const FunctionDeclarationNode* dereference_func) const;
	ASTNode normalizeRangedForLoopDecl(const RangedForStatementNode& stmt);
	const FunctionDeclarationNode* resolveRangedForIteratorDereference(
		const TypeSpecifierNode& iterator_type,
		bool prefer_const) const;

private:
 // Top-level dispatch
	void normalizeTopLevelNode(const ASTNode& node);

 // Declaration handlers
	void normalizeFunctionDeclaration(const FunctionDeclarationNode& func);
	void normalizeConstructorDeclaration(const ConstructorDeclarationNode& ctor);
	void normalizeDestructorDeclaration(const DestructorDeclarationNode& dtor);
	void normalizeStructDeclaration(const StructDeclarationNode& decl);
	void normalizeNamespace(const NamespaceDeclarationNode& ns);

 // Statement handler
	void normalizeStatement(const ASTNode& node, const SemanticContext& ctx);
	void normalizeBlock(const BlockNode& block, const SemanticContext& ctx);

 // Expression handler (counts and infers types for annotatable expressions)
	SemanticExprInfo normalizeExpression(const ASTNode& node, const SemanticContext& ctx);

 // Helpers
	void registerParametersInScope(const std::vector<ASTNode>& parameter_nodes);
	CanonicalTypeId canonicalizeType(const TypeSpecifierNode& type);
	void resolveRemainingAutoReturns();
	void resolveRemainingAutoReturnsInNode(ASTNode& node);
	std::optional<TypeSpecifierNode> deducePlaceholderReturnType(const ASTNode& body, TypeCategory placeholder_type);
	TypeSpecifierNode finalizePlaceholderDeduction(TypeCategory placeholder_type, const TypeSpecifierNode& deduced_type) const;

 // Infer the canonical type of a simple expression without full evaluation.
 // Handles: NumericLiteralNode, BoolLiteralNode, IdentifierNode (via scope stack).
 // Returns invalid CanonicalTypeId if inference is not possible.
	CanonicalTypeId inferExpressionType(const ASTNode& node);
	void registerOuterTemplateBindingsInScope(const LambdaExpressionNode& lambda);
	void registerOuterTemplateBindingsInScope(const LambdaInfo& lambda_info);
	void registerOuterTemplateBindingsInScope(const StructDeclarationNode& decl);
	void registerOuterTemplateBindingsInScope(const VariableDeclarationNode& var);
	void registerOuterTemplateBindingsInScope(const FunctionDeclarationNode& func);
	void registerOuterTemplateBindingsInScope(const ConstructorDeclarationNode& ctor);
	void registerOuterTemplateBindingsInScope(const DestructorDeclarationNode& dtor);
	std::optional<TypeSpecifierNode> buildOverloadResolutionArgType(
		const ASTNode& arg,
		CanonicalTypeId* inferred_type_id = nullptr);

 // Allocate a new ImplicitCastInfo entry and return its 1-based index.
	CastInfoIndex allocateCastInfo(const ImplicitCastInfo& info);
	CastInfoIndex allocateNonUserDefinedCastInfo(CanonicalTypeId source_type_id,
												 CanonicalTypeId target_type_id,
												 StandardConversionKind cast_kind);

 // Store a semantic slot for the given expression node pointer.
	void setSlot(const void* key, const SemanticSlot& slot);

 // Core annotation helper: if the inferred type of expr_node differs from target_type_id
 // and a standard (non-user-defined) primitive conversion applies, allocate an
 // ImplicitCastInfo and fill the expression's SemanticSlot.
 // Returns true when a slot was filled.
	bool tryAnnotateConversion(const ASTNode& expr_node,
							   CanonicalTypeId target_type_id,
							   CanonicalTypeId expr_type_id = {});
	bool tryAnnotateCopyInitConvertingConstructor(const ASTNode& expr_node,
												  CanonicalTypeId target_type_id,
												  const char* context_description,
												  CanonicalTypeId expr_type_id = {});

 // Try to annotate a return expression with implicit cast info when the
 // expression type differs from the declared function return type.
	void tryAnnotateReturnConversion(const ASTNode& expr_node, const SemanticContext& ctx);

 // Annotate binary arithmetic/comparison operands with their common-type conversions.
 // The caller may pass precomputed operand type IDs to avoid re-running inference on hot paths.
	void tryAnnotateBinaryOperandConversions(const BinaryOperatorNode& bin_op,
											 CanonicalTypeId lhs_type_id = {}, CanonicalTypeId rhs_type_id = {});

 // C++20 [expr.ass]/7: for compound assignment E1 op= E2, the result of the
 // arithmetic is converted back from the common type to the LHS type
 // (equivalent to static_cast<T1>(E1 op E2)).  Annotate this on the
 // BinaryOperatorNode so codegen can verify sema ownership.
	void tryAnnotateCompoundAssignBackConversion(const BinaryOperatorNode& bin_op,
												 CanonicalTypeId lhs_type_id, CanonicalTypeId rhs_type_id);

 // Shared helper: build a back-conversion SemanticSlot from source_type → target_type
 // and store it in compound_assign_back_conv_ keyed on &bin_op.
	void storeCompoundAssignBackConvSlot(const BinaryOperatorNode& bin_op,
										 CanonicalTypeId source_type_id, CanonicalTypeId target_type_id);

 // C++20 [expr.shift]: shift operands undergo independent integral promotions,
 // NOT the usual arithmetic conversions.  Each operand is promoted separately
 // (bool/char/short → int); the result type is the promoted LHS type.
	void tryAnnotateShiftOperandPromotions(const BinaryOperatorNode& bin_op,
										   CanonicalTypeId lhs_type_id = {}, CanonicalTypeId rhs_type_id = {});

 // C++20 [expr.unary.op]: the operand of unary +, -, and ~ undergoes integral
 // promotion.  Types with conversion rank less than int (bool, char, short, etc.)
 // are promoted to int before the operator is applied.
	void tryAnnotateUnaryOperandPromotion(const UnaryOperatorNode& unary_op);

 // Annotate an expression with contextual bool conversion (C++20 [conv.bool]).
 // Used for control-flow conditions (if/while/for/do-while), ternary condition,
 // and logical operator operands (&&, ||).
	void tryAnnotateContextualBool(const ASTNode& expr_node);

 // Annotate function-call arguments with their parameter-type conversions.
	void tryAnnotateCallArgConversions(const FunctionCallNode& call_node);

 // Annotate member-function-call arguments with their parameter-type conversions.
	void tryAnnotateMemberFunctionCallArgConversions(const MemberFunctionCallNode& call_node);
	std::optional<CallArgReferenceBindingInfo> buildCallArgReferenceBinding(const ASTNode& arg,
																			const TypeSpecifierNode& param_type,
																			const char* context_description);

 // Annotate constructor-call arguments with their parameter-type conversions.
	void tryAnnotateConstructorCallArgConversions(const ConstructorCallNode& call_node);

 // Annotate InitializerListNode elements used as constructor arguments
 // with their parameter-type conversions (for direct-init syntax like `Type obj(args...)`).
	void tryAnnotateInitListConstructorArgs(const InitializerListNode& init_list, const StructTypeInfo& struct_info);

 // Annotate ternary operator branches with common-type conversions
 // (C++20 [expr.cond]/7: usual arithmetic conversions on the second and third operands).
	void tryAnnotateTernaryBranchConversions(const TernaryOperatorNode& ternary_node);

 // Resolve the callable operator() for a FunctionCallNode whose callee is a struct-typed
 // variable (functor / closure). Stores the result in op_call_table_ so that codegen can
 // consume it without performing its own member-function lookup.
	void tryResolveCallableOperator(const FunctionCallNode& call_node);

 // Scope stack for local variable type tracking (used by inferExpressionType).
 // Keys are StringHandles from the string pool (stable for the compilation lifetime).
	void pushScope();
	void popScope();
	void addLocalType(StringHandle name, CanonicalTypeId type_id);
	CanonicalTypeId lookupLocalType(StringHandle name) const;

 // Diagnose implicit conversion from scoped enum.
 // C++11+: scoped enums do not allow implicit conversion to other types.
 // Throws CompileError if expr_node is a scoped enum and target type differs.
	void diagnoseScopedEnumConversion(const ASTNode& expr_node,
									  CanonicalTypeId target_type_id,
									  const char* context_description,
									  CanonicalTypeId expr_type_id = {});

 // Diagnose scoped enum used as operand in binary arithmetic/comparison with a
 // different type.  Per C++20, scoped enums only support relational/equality
 // operators between values of the same scoped enum type.
	void diagnoseScopedEnumBinaryOperands(const BinaryOperatorNode& bin_op,
										  CanonicalTypeId lhs_type_id = {}, CanonicalTypeId rhs_type_id = {});

 // State
	Parser& parser_;
	CompileContext& context_;
	SymbolTable& symbols_;
	TypeContext type_context_;
	CanonicalTypeId bool_type_id_{};	 // Cached canonical type for bool (interned once in constructor).
	std::vector<ImplicitCastInfo> cast_info_table_;
	SemanticPassStats stats_;

 // Side table: expression node pointer → semantic slot.
	std::unordered_map<const void*, SemanticSlot> semantic_slots_;

 // Side table: BinaryOperatorNode pointer → back-conversion slot for compound assignments.
 // Stores the commonType→lhsType conversion that C++20 [expr.ass]/7 requires after the
 // arithmetic is performed in the promoted common type.
	std::unordered_map<const void*, SemanticSlot> compound_assign_back_conv_;

 // Side table: FunctionCallNode pointer → resolved operator() declaration.
 // Populated by tryResolveCallableOperator for struct-typed callable objects.
	std::unordered_map<const FunctionCallNode*, const FunctionDeclarationNode*> op_call_table_;
	std::unordered_map<const FunctionCallNode*, std::vector<CallArgReferenceBindingInfo>> function_call_ref_bindings_;
	std::unordered_map<const MemberFunctionCallNode*, std::vector<CallArgReferenceBindingInfo>> member_call_ref_bindings_;

 // Track which function body ASTNode pointers sema has normalized.
 // Codegen uses this to skip Phase 15 warnings for functions sema never visited
 // (e.g. template instantiation member functions generated during parsing).
	std::unordered_set<const void*> normalized_bodies_;

 // Track FunctionCallNode pointers where sema attempted call-arg annotation
 // but couldn't resolve the callee (e.g. template specialization static members
 // whose DeclarationNode addresses differ from the call's stored decl).
	std::unordered_set<const FunctionCallNode*> unresolved_call_args_;

 // Scope stack: each entry maps local variable StringHandle → canonical type id.
	std::vector<std::unordered_map<StringHandle, CanonicalTypeId>> scope_stack_;

 // Enclosing implicit-member context for bodies/default initializers currently
 // being normalized. Used to type `IdentifierBinding::NonStaticMember`
 // sema-first instead of falling back to parser-owned member-context lookup.
	std::vector<TypeIndex> member_context_stack_;
};
