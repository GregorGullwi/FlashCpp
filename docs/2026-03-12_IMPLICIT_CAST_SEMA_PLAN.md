# Semantic-analysis separation plan

## Problem statement

FlashCpp currently goes from `Parser::parse()` almost directly into `AstToIr`, with semantic decisions split between:

- parser-time heuristics and partial type deduction
- syntax-level type comparisons in `TypeSpecifierNode` / `SymbolTable`
- codegen-local conversion logic in `AstToIr`

There is already a repo proposal to introduce a separate semantic-analysis stage. After reviewing the current codebase, the proposal is directionally correct from a C++20 perspective, but it should be narrowed and staged more carefully for both implementation risk and compile-time performance.

## Recommendation summary

### Recommended direction

Introduce a **post-parse semantic-normalization pass**, not a full “move all semantics out of the parser” rewrite.

That means:

1. Keep the parser responsible for syntax construction, symbol-table population, template-instantiation triggers, and the existing deferred-body/template machinery for now.
2. Add a focused semantic stage between parse and IR lowering that:
	- canonicalizes expression meaning where C++20 requires semantic context
	- fills preallocated semantic slots / annotations on existing AST nodes for the common standard-conversion cases
	- reserves structural semantic nodes only for later cases that cannot be represented compactly
	- removes ad-hoc conversion policy from `AstToIr`
3. Delay any larger “full semantic model / separate type system” effort until the normalization pass is proven and measured.

### Why this is the right reading of the existing docs

The existing docs are right that implicit conversions, value-category changes, contextual `bool`, and canonical type identity are semantic concerns and do not belong in parser-inline logic.

However, if “separate semantics” is interpreted as “stop doing semantic work during parsing,” that is too large and too risky for the current architecture because:

- template instantiation already happens during parsing
- parser-owned state already feeds symbol/type formation
- delayed function bodies and template substitution are deeply integrated with parser machinery
- some semantic-ish metadata is already stored on syntax nodes (`BinaryOperatorNode` overload resolution state, `IdentifierNode` bindings, `TypeSpecifierNode` type indices)

So the right first milestone is **semantic normalization**, not an immediate Clang-style full Sema subsystem.

## Current-state findings

### Pipeline

- ~~`main.cpp` does `parser->parse();` and then constructs `AstToIr` with no semantic pass in between.~~
- ~~There is currently no `runSemanticAnalysis(...)` seam in the pipeline.~~
- **Updated (PR #917):** `FlashCppMain.cpp` now runs `SemanticAnalysis::run()` between `gLazyMemberResolver.clearCache()` and `AstToIr` construction. The pass is timed separately and stats are emitted under `--perf-stats`. Phase 1 is a no-op traversal that validates the pipeline seam.

### Semantic decisions currently split across layers

- `IrGenerator_Call_Direct.cpp` applies implicit argument conversions with `generateTypeConversion(...)`.
- `IrGenerator_Expr_Operators.cpp` applies assignment conversion and usual arithmetic conversions inline.
- `IrGenerator_Visitors_Namespace.cpp` applies return conversions inline.
- `Parser_Statements.cpp` already deduces some `auto` declarations during parsing.
- `IrGenerator_Expr_Primitives.cpp` still has a transitional `Type::Auto && size_bits == 0 -> int/32-bit` fallback.

### Syntax-level nodes currently carrying semantic load

- `TypeSpecifierNode::matches_signature(...)` performs function-signature equivalence on syntax metadata.
- `SymbolTable::lookup_function(...)` still has a simplified two-phase selection path in addition to `OverloadResolution.h`.
- `BinaryOperatorNode` already stores semantic operator-resolution results.

### Existing architecture that helps the plan

- AST nodes live in stable chunk storage (`gChunkedAnyStorage`).
- `ASTNode` mostly passes around pointers into that storage.
- `ExpressionSubstitutor` already performs subtree transformation by cloning only touched nodes into the same arena.

This means the plan does **not** need expensive whole-AST mutation or rebuilding. The low-cost path is:

- parser-built nodes start with empty semantic slots (`no cast`, `no adjustment`, etc.)
- semantic pass fills those slots in place where compact metadata is enough
- only allocate replacement / structural nodes for later cases that cannot be encoded as annotation

## Evaluation from a C++20 standards perspective

## What is correct about a semantic pass

A separate post-parse semantic stage is the right architectural home for:

- standard implicit conversions (`[conv]`)
- usual arithmetic conversions (`[expr]`, `[conv.arith]`)
- contextual conversion to `bool`
- reference binding and temporary materialization
- return conversion
- initialization conversion
- canonical type identity for overload/signature comparison

These are semantic questions because they depend on resolved types, value categories, overload ranking, and surrounding context. They are not purely syntactic.

## What should stay out of the first pass

The first pass should **not** try to re-own all of:

- unqualified and qualified lookup
- template instantiation strategy
- parser-driven deferred-body mechanics
- every aspect of type formation

Those can evolve later, but trying to do all of them now would turn a good architectural cleanup into a major compiler rewrite.

## Standards-risk areas the plan should explicitly cover

The implementation plan should include explicit handling for:

- lvalue-to-rvalue conversion
- array-to-pointer / function-to-pointer decay
- promotions vs conversions
- enum behavior (preserve enum identity where required; convert only in the contexts permitted by the standard)
- qualification adjustments
- reference binding rules
- temporary materialization
- contextual `bool`
- overload ranking data that distinguishes exact match / promotion / conversion / user-defined conversion

## Other strong candidates for later semantic phases

Beyond the items already called out, these also fit naturally in a semantic-analysis layer:

- conditional-operator (`?:`) common-type and value-category resolution
	- today `get_expression_type(...)` in `Parser_Expr_QualLookup.cpp` still uses a simplified “larger bit width wins” rule for arithmetic branches and a fallback-to-true-branch rule for mixed cases
- copy-init vs direct-init vs list-init semantic distinctions
	- especially list-initialization narrowing checks and “is an explicit constructor/conversion viable in this context?” decisions
- deleted / explicit / defaulted function and constructor viability
	- this is part of semantic overload viability, not a parser concern
- `noexcept` / exception-spec semantics
	- classification of `noexcept(expr)`, consistency of exception-spec metadata, and any later overload/function-type rules that depend on it
- constant-expression-required contexts
	- array bounds, case labels, enumerator initializers, `if constexpr`, `noexcept`, and similar “must be a constant expression here” rules
- constructor/member/base initializer semantic validation
	- delegating-constructor rules, base/member initializer legality, duplicate initialization diagnostics, and related checks
- concept / constraint / requires-expression satisfaction
	- if concepts support grows, semantic analysis is the right layer for post-substitution constraint checking
- copy-elision / lifetime-extension policy
	- broader than temporary materialization alone; this becomes the right home once the compiler wants more faithful C++ object-lifetime semantics

These are all good fits, but most are **later-semantic-layer work**, not phase-1 normalization work.

## Evaluation from a performance perspective

## Main conclusion

Your concern is valid **if** the design assumes whole-tree in-place rewriting or repeated re-analysis of the same subtrees.

That is **not** the design I recommend.

## Recommended performance shape

Use an **annotation-first semantic normalization** on top of the current AST:

- parser-created expression nodes carry empty semantic slots by default
- semantic pass fills those slots in place for standard conversions and value-category normalization
- rebuild only the minimal owning subtree when a later semantic feature truly requires structural change
- reuse untouched `ASTNode` pointers everywhere else

This is compatible with the current `gChunkedAnyStorage` model, and it answers the performance concern better than universal wrapper-node insertion.

## Performance rules for the implementation

1. Do not copy the full translation unit AST.
2. Do not eagerly canonicalize every node into a second full semantic tree.
3. Prefer preallocated semantic slots / annotations over wrapper nodes for the common implicit-conversion cases.
4. The per-expression semantic slot should be **heavily packed** (target: about 8 bytes, certainly not “store CanonicalType by value in every node”).
5. Use type interning so AST nodes carry compact `CanonicalTypeId` / handles rather than full canonical-type payloads.
6. Use side data or structural semantic nodes only when compact in-node metadata is not enough.
7. Time the new pass separately in `main.cpp`.
8. Track pass-time deltas and how often semantics fit in slots versus requiring structural nodes.

### Memory-layout requirement

This is the single most important refinement to the annotation-first design.

Adding something like:

- `std::optional<ImplicitCastInfo>`
- where `ImplicitCastInfo` embeds two `CanonicalType` values

directly into every expression node would likely destroy cache locality and inflate AST memory dramatically.

So the plan should explicitly forbid that shape.

The semantic slot stored on AST nodes should instead be a **compact handle layer**, for example:

```cpp
struct CanonicalTypeId {
	uint32_t value = 0;
	explicit constexpr operator bool() const { return value != 0; }
};

struct CastInfoIndex {
	uint16_t value = 0;
	explicit constexpr operator bool() const { return value != 0; }
};

enum class SemanticSlotFlags : uint8_t {
	None = 0,
	IsDependent = 1 << 0,
	IsConstantEvaluated = 1 << 1,
	IsOverloadSet = 1 << 2,
};

struct SemanticSlot {
	CanonicalTypeId type_id{};                // Interned canonical type
	CastInfoIndex cast_info_index{}; // Invalid = no cast
	ValueCategory value_category = ValueCategory::PRValue; // Reuse existing enum in IRTypes_Registers.h
	SemanticSlotFlags flags = SemanticSlotFlags::None;
};
```

Here `ValueCategory` should be treated as an existing shared enum, not a new semantic-analysis-local type. For slot packing, the shared enum should use a narrow underlying type (`enum class ValueCategory : uint8_t`).

If a slot cannot be kept compact enough, the fallback should be:

- a side table keyed by `ASTNode` identity, or
- a tiny per-node handle into side storage

but **not** a large by-value semantic payload embedded into all expression nodes.

## Recommended architecture

### 1. Add a narrow semantic-normalization layer

New entry point:

```cpp
runSemanticAnalysis(*parser, context);
```

Pipeline target:

1. parse
2. clear parser-side lazy caches as today
3. run semantic normalization
4. lower normalized AST in `AstToIr`

### 2. Add semantic slots / annotations first; reserve structural nodes for later

#### Target first-step data model

```cpp
enum class StandardConversionKind : uint8_t {
	LValueToRValue,
	ArrayToPointer,
	FunctionToPointer,
	IntegralPromotion,
	FloatingPromotion,
	IntegralConversion,
	FloatingConversion,
	FloatingIntegralConversion,
	BooleanConversion,
	QualificationAdjustment,
	DerivedToBase,
	UserDefined,
	TemporaryMaterialization
};

struct CanonicalTypeId {
	uint32_t value = 0;
	explicit constexpr operator bool() const { return value != 0; }
	friend constexpr bool operator==(CanonicalTypeId, CanonicalTypeId) = default;
};

struct CastInfoIndex {
	uint16_t value = 0;
	explicit constexpr operator bool() const { return value != 0; }
	friend constexpr bool operator==(CastInfoIndex, CastInfoIndex) = default;
};

enum class SemanticSlotFlags : uint8_t {
	None = 0,
	IsDependent = 1 << 0,
	IsConstantEvaluated = 1 << 1,
	IsOverloadSet = 1 << 2,
};

enum class CanonicalTypeFlags : uint8_t {
	None = 0,
	IsPackExpansion = 1 << 0,
	IsFunctionType = 1 << 1,
};

struct CanonicalTypeDesc {
	Type base_type;
	TypeIndex type_index;
	CVQualifier base_cv = CVQualifier::None;
	ReferenceQualifier ref_qualifier;
	InlineVector<PointerLevel, 4> pointer_levels;
	InlineVector<size_t, 4> array_dimensions;
	CanonicalTypeFlags flags = CanonicalTypeFlags::None;
	std::optional<FunctionSignature> function_signature;
};

class TypeContext {
public:
	CanonicalTypeId intern(const CanonicalTypeDesc& desc);
	const CanonicalTypeDesc& get(CanonicalTypeId id) const;
};

struct ImplicitCastInfo {
	CanonicalTypeId source_type_id;
	CanonicalTypeId target_type_id;
	StandardConversionKind cast_kind;
	ValueCategory value_category_after = ValueCategory::PRValue; // Reuse existing enum
};

struct SemanticSlot {
	CanonicalTypeId type_id{};
	CastInfoIndex cast_info_index{};
	ValueCategory value_category = ValueCategory::PRValue;
	SemanticSlotFlags flags = SemanticSlotFlags::None;
};

enum class RewriteDisposition : uint8_t {
	Unchanged,
	StructurallyChanged,
};

struct SemanticRewriteResult {
	ASTNode node;
	CanonicalTypeId type_id{};
	ValueCategory value_category;
	RewriteDisposition disposition = RewriteDisposition::Unchanged;
};
```

`SemanticSlot` should stay compact, but there is no strong reason to fold `ValueCategory` into `SemanticSlotFlags`.

Recommendation:

- keep `value_category` as its own field
- keep `flags` for orthogonal yes/no semantic facts

Why:

- `ValueCategory` is not a flag set; it is a mutually exclusive state
- keeping it separate is clearer and more type-safe
- with a shared `: uint8_t` underlying type, it is already compact enough that folding it into generic flags adds complexity with little or no meaningful memory win

Interned canonical types are important because the long-term bug class here is “syntax metadata pretending to be semantic type identity.”

#### Implementation note for the current AST

The current AST does not have a shared expression-node base class, so the plan should target one of these implementation shapes:

1. preferred target: add a **small packed semantic-slot member** to each expression node class
2. acceptable transition: keep a side table keyed by `ASTNode` identity while converging on in-node packed slots

Before changing many node layouts, measure:

- `sizeof` the main expression node alternatives before and after
- total AST memory on representative translation units

If adding a slot to every expression node materially harms memory locality, use the side-table approach longer.

Type-safety rule:

- do not use raw `uint32_t` / `uint16_t` aliases for semantic handles if the codebase can accidentally mix them
- prefer small wrapper structs with explicit construction and no arithmetic
- prefer `enum class` or typed policy enums over free booleans for semantic control flow

What should **not** be the default first step is inserting a dedicated `NullImplicitCastNode` wrapper around every expression.

#### Structural nodes to reserve for later

Only introduce explicit structural semantic nodes if a compact slot cannot faithfully encode the semantic effect:

- `MaterializeTemporaryNode`
- `BoundTemporaryNode`
- `ImplicitObjectAdjustmentNode`

### 3. Add a dedicated semantic pass class

Recommended new files:

- `src/SemanticAnalysis.h`
- `src/SemanticAnalysis.cpp`

Recommended class shape:

```cpp
class SemanticAnalysis {
public:
	SemanticAnalysis(Parser& parser, CompileContext& context, SymbolTable& symbols);

	void run();

private:
	ASTNode normalizeTopLevelSemantics(ASTNode node);
	ASTNode normalizeStatementSemantics(ASTNode node, const SemanticContext& ctx);
	SemanticRewriteResult normalizeExpressionSemantics(ASTNode node, const SemanticContext& ctx);

	SemanticRewriteResult applyImplicitConversion(
		const SemanticRewriteResult& expr,
		CanonicalTypeId target_type_id,
		ConversionContext context);

	CanonicalTypeId canonicalizeType(const TypeSpecifierNode& type) const;
	CanonicalTypeId getExpressionType(const ASTNode& node, const SemanticContext& ctx) const;
};
```

### Naming guidance

The earlier `rewriteTopLevel` / `rewriteStatement` / `rewriteExpression` names describe the mechanism, but not the purpose.

For this pass, prefer intent-revealing names such as:

- `normalizeTopLevelSemantics(...)`
- `normalizeStatementSemantics(...)`
- `normalizeExpressionSemantics(...)`

`rewrite*` is still reasonable as a local helper name, but it should not be the main API vocabulary for a semantic-normalization pass.

### 4. Prefer selective subtree replacement over in-place mutation

Because many AST nodes expose getters but not setters (`ReturnStatementNode`, `IfStatementNode`, `BinaryOperatorNode`, `UnaryOperatorNode`, `TernaryOperatorNode`, etc.), the implementation should use a mixed strategy:

- prefer in-place semantic-slot updates when only expression semantics changed
- rewrite children recursively when a child subtree changes structurally
- if no child changed structurally, keep the original node
- only allocate a replacement node in `gChunkedAnyStorage` when the tree shape truly has to change

This avoids both a large mutability refactor and the cost of universal wrapper-node insertion.

## Classes and files likely involved

### New or newly central classes

- `SemanticAnalysis`
- `StandardConversionKind`
- `ValueCategory`
- `CanonicalTypeDesc`
- `TypeContext`
- `CanonicalTypeId`
- `ImplicitCastInfo`
- `SemanticSlot`
- `SemanticRewriteResult`
- `SemanticContext`

### Existing classes that the pass must integrate with

- `Parser`
- `SymbolTable`
- `OverloadResolution`
- `TypeSpecifierNode`
- `DeclarationNode`
- `FunctionCallNode`
- `ConstructorCallNode`
- `BinaryOperatorNode`
- `UnaryOperatorNode`
- `TernaryOperatorNode`
- `ReturnStatementNode`
- `IfStatementNode`
- `ForStatementNode`
- `WhileStatementNode`
- `DoWhileStatementNode`
- `InitializerListNode`
- `LambdaInfo`
- `AstToIr`
- `ExpressionSubstitutor`

### Files to modify in the first serious implementation slice

- `src/main.cpp`
	- insert and time the semantic pass
- `src/AstNodeTypes_Expr.h`
	- add semantic-slot storage for expression nodes
	- add `ImplicitCastInfo` / related semantic metadata types
- `src/AstNodeTypes.cpp`
	- AST string/debug printer support
- `src/AstToIr.h`
	- add lowering hooks for semantic annotations
- `src/IrGenerator_Expr_Conversions.cpp`
	- make conversion lowering serve semantic annotations instead of owning policy
- `src/IrGenerator_Call_Direct.cpp`
	- delete direct argument-conversion policy once pass covers it
- `src/IrGenerator_Expr_Operators.cpp`
	- delete arithmetic/assignment conversion policy once pass covers it
- `src/IrGenerator_Visitors_Namespace.cpp`
	- delete return-conversion policy once pass covers it

### Files for later semantic cleanups

- `src/OverloadResolution.h`
	- reuse / refine ranking and conversion classification
- `src/SymbolTable.h`
	- gradually stop relying on syntax-level signature heuristics
- `src/AstNodeTypes_DeclNodes.h`
	- reduce `TypeSpecifierNode::matches_signature(...)` responsibilities
- `src/Parser_Statements.cpp`
	- move `auto` declaration normalization out over time
- `src/IrGenerator_Expr_Primitives.cpp`
	- remove transitional `Type::Auto` fallback
- `src/IrGenerator_Lambdas.cpp`
	- remove synthetic declaration workaround once generic-lambda normalization moves earlier

## Detailed data transformations to plan for

### A. Function-call arguments

Current state:

- parse builds `FunctionCallNode`
- overload resolution chooses a callee
- codegen converts arguments inline

Target state:

1. rewrite each argument expression
2. compute canonical parameter type
3. fill `ImplicitCastInfo` on the argument expression if the chosen overload requires conversion
4. build a replacement `FunctionCallNode` only if an argument subtree changed structurally
5. `AstToIr` lowers already-normalized arguments

### B. Binary arithmetic and comparisons

Current state:

- `IrGenerator_Expr_Operators.cpp` computes common type and emits conversions inline

Target state:

1. rewrite `lhs`
2. rewrite `rhs`
3. preserve any already-recorded operator-overload resolution metadata
4. if builtin arithmetic path applies, compute common canonical type
5. annotate one or both operands with implicit-cast metadata
6. lower directly from normalized operands

### C. Assignment

Current state:

- assignment converts RHS to LHS type in codegen

Target state:

1. rewrite `lhs` and `rhs`
2. compute target type from `lhs`
3. annotate `rhs` with the needed standard conversion metadata; reserve structural nodes for later user-defined / lifetime-sensitive cases
4. leave IR lowering as a straightforward assignment

### D. Return statements

Current state:

- return conversion is applied in `IrGenerator_Visitors_Namespace.cpp`

Target state:

1. semantic pass tracks the current function return type
2. return expression is rewritten
3. conversion metadata is attached at the expression boundary; only later lifetime-sensitive cases should require structural nodes
4. `AstToIr` trusts the rewritten return expression

### E. Contextual `bool`

Affected nodes:

- `IfStatementNode`
- `WhileStatementNode`
- `ForStatementNode`
- `DoWhileStatementNode`
- `TernaryOperatorNode`
- logical operators if represented on the builtin path

Target transformation:

- rewrite condition expression
- attach implicit-bool conversion metadata when contextual conversion applies

### F. Initializers and declarations

Current state:

- parser and codegen split responsibility

Target state:

1. declaration type is canonicalized
2. initializer expression is rewritten
3. semantic pass inserts conversion/reference-binding/temporary materialization nodes
4. codegen stops guessing the policy from local context

### G. `auto` and generic lambda normalization

This should be treated as a **follow-up phase**, not phase 1.

Reason:

- some `auto` deduction already happens during parsing
- generic lambdas still rely on `LambdaInfo`-carried deduced types, but instantiated-body placeholder normalization now runs through `SemanticAnalysis::normalizeInstantiatedLambdaBody(...)` instead of codegen-local synthetic declarations
- instantiated bodies may need a semantic hook after instantiation, not only once per translation unit

Recommended target:

- translation-unit semantic pass for ordinary declarations and expressions
- per-instantiation semantic hook for instantiated generic lambda / template bodies before `AstToIr`

#### 1. Ordinary `auto` variable declarations

Current state:

- parser already deduces `auto` from the initializer in `Parser_Statements.cpp`
- it preserves cv/ref qualifiers and some special cases such as captureless-lambda-to-function-pointer deduction

Architectural fit:

- in the **near term**, this is one of the few semantic operations that can reasonably stay parser-side, because later parsing and symbol-table lookup in the same scope may depend on the deduced declared type
- the semantic pass should still **validate and canonicalize** the parser-produced type, and it should own any initializer-conversion annotations that apply after deduction

Recommendation:

- do **not** force ordinary `auto` variable deduction out of the parser in the first semantic-pass rollout
- instead, treat parser deduction as the producer of a provisional concrete `TypeSpecifierNode`
- let the semantic pass normalize the initializer semantics around that type

Longer term:

- only move ordinary `auto` variable deduction fully into semantic analysis if the parser no longer needs the final type for same-scope lookup / expression typing during parse

#### 2. Generic lambda `auto` parameters

Current state:

- call-site deduction is stored through `LambdaInfo::setDeducedType(...)`
- semantic normalization now rewrites instantiated generic-lambda parameter declarations from placeholder `Type::Auto` to the deduced concrete `TypeSpecifierNode` before lowering
- generic-lambda return-type finalization for instantiated bodies also runs in that semantic hook
- unresolved placeholder types that still reach codegen are now treated as internal errors instead of silently falling back to `int`/32-bit
- remaining risk is stale parser/mangling consumers reading pre-normalized signature data instead of the sema-normalized instantiated lambda state

Architectural fit:

- this does **not** belong in the translation-unit semantic pass
- it belongs in an **instantiation-time semantic normalization hook** that runs:
	1. after call-site deduction succeeded
	2. after the instantiated lambda body exists
	3. before `AstToIr` lowers `operator()` / `__invoke`

Recommendation:

- keep parser ownership of generic-lambda syntax
- keep call-site deduction ownership near overload/call resolution initially
- move the *body/signature normalization* out of codegen and into a dedicated instantiation-time semantic step

That semantic step should:

- rewrite parameter declarations from placeholder `Type::Auto` to the deduced concrete `TypeSpecifierNode`
- update the lambda body’s symbol table view so identifier lookup sees the concrete parameter declarations
- update mangling/signature generation inputs to use normalized types
- eliminate the need for synthetic deduced declarations in `IrGenerator_Lambdas.cpp`
- eliminate the fallback-to-`int` behavior in `IrGenerator_Expr_Primitives.cpp`

#### 3. Non-generic lambda `auto` return type

Current state:

- parser already scans lambda bodies and deduces / validates the return type in `Parser_Expr_ControlFlowStmt.cpp`

Architectural fit:

- for non-generic lambdas, this can remain parser-side in the near term
- by the time the translation-unit semantic pass runs, the lambda usually already has a concrete return type

Recommendation:

- keep parser-side deduction initially
- semantic pass should consume the deduced type as canonical input and handle any later conversion normalization in the lambda body

#### 4. Ordinary function `auto` return type

Current state:

- parser has `deduce_and_update_auto_return_type(...)` in `Parser_Expr_QualLookup.cpp`
- `SemanticAnalysis::resolveRemainingAutoReturns()` now finalizes unresolved ordinary function placeholder returns before lowering
- parser-side mangling still needs to defer while any part of the function signature contains unresolved placeholder `auto` / `decltype(auto)` so downstream consumers do not see stale mangled names

Architectural fit:

- with the current architecture, parser-side deduction is still useful because function signatures may be queried during later parsing
- however, codegen should not remain the place that “finishes” the language rule

Recommendation:

- near term: keep parser-side function return deduction as the primary producer
- semantic pass should validate/canonicalize the final function return type before lowering
- remove the codegen fallback once the parser + semantic pass together guarantee that supported functions reach codegen with concrete return types

Longer term:

- full migration of function `auto` return deduction into semantic analysis is possible, but only after the parser no longer depends on the finalized return type to answer later semantic-ish queries

#### 5. `decltype(auto)`

Current state:

- `Type::DeclTypeAuto` now distinguishes `decltype(auto)` from plain `auto`
- parser-side declarator handling preserves the deduced size/category information and rejects extra declarator wrappers (`*`, `&`, `&&`) around `decltype(auto)`

Architectural fit:

- `decltype(auto)` is more naturally a semantic-analysis feature than plain `auto`, because it depends on the final expression type **and value category**
- it should not be treated as “just another auto case”

Recommendation:

- keep `decltype(auto)` as a later dedicated subphase
- introduce explicit metadata distinguishing plain `auto` from `decltype(auto)` even if both temporarily use `Type::Auto`
- resolve `decltype(auto)` only when the expression’s canonical type and value category are known

#### Bottom-line fit

The right split is:

- **parser-adjacent for now**:
	- ordinary `auto` variable deduction
	- non-generic lambda `auto` return deduction
	- primary function `auto` return deduction
- **translation-unit semantic pass**:
	- validation/canonicalization of parser-produced `auto` results
	- initializer/return conversion semantics around those deduced types
- **instantiation-time semantic hook**:
	- generic lambda parameter normalization
	- any generic-lambda return normalization that depends on deduced parameter types
- **later dedicated semantic phase**:
	- `decltype(auto)` cleanup

## Phased implementation plan

### Phase 1: establish the seam ✅ (PR #917)
- Added `SemanticAnalysis` pass seam, `SemanticSlot` per-expression storage, `CastInfoTable`, and `CanonicalTypeId` interning.
- New tests: arithmetic/comparison/return conversions produce correct output.

### Phase 2: return, call-arg, binary-op contexts ✅
- `tryAnnotateReturnConversion`: return-type mismatch annotated with cast.
- `tryAnnotateCallArgConversions`: call arguments annotated with conversions.
- `tryAnnotateBinaryOperandConversions`: binary arithmetic/comparison operands annotated to common type.
- Codegen dual-path: sema annotation wins; codegen policy is fallback.
- Test suite: return-value and binary-op conversions verified.

### Phase 3: declaration initializers + assignment RHS ✅
- `normalizeStatement` annotates `VariableDeclarationNode` init with target type.
- `normalizeExpression` annotates `=` RHS with LHS type.
- `inferExpressionType` extended to `BinaryOperatorNode`, `FunctionCallNode`.
- Tests: `test_decl_init_implicit_cast_ret0.cpp`.

### Phase 4: canonical type identity cleanup ✅
- `TypeContext::intern()` uses `std::unordered_map` (O(1) vs O(n)).
- `inferExpressionType` handles cast nodes.
- `tryAnnotateCallArgConversions` uses `lookup_all` + pointer-identity overload matching.
- Tests: `test_cast_expr_type_inference_ret0.cpp`, `test_overload_call_annotation_ret0.cpp`.

### Phase 5: `auto` / generic lambda cleanup ✅ (with PR928 hardening)
- `Type::DeclTypeAuto` distinct from `Type::Auto`.
- `resolveRemainingAutoReturns()` finalizes ordinary function placeholder returns.
- `normalizeInstantiatedLambdaBody()` owns generic-lambda normalization before IR.
- Callable `operator()` resolution uses `resolve_overload` (type-based) in parser and sema; arity-only retained as fallback.
- Ambiguous overloads now detected and reported.
- Tests: range-for, auto-return, generic lambda, callable overload disambiguation.

### Phase 6: contextual bool conversion ✅
- `tryAnnotateContextualBool` annotates conditions (if/while/for/do-while/ternary) and `&&`/`||`/`!` operands.
- `applyConditionBoolConversion` in codegen: float/double conditions use `FloatNotEqual(cond, 0.0)` (handles `-0.0`, fractional values, NaN); integers use backend `TEST`.
- Tests: `contextual_bool_{int,float,char,logical,neg_zero,not_neg_zero,fractional_float}`.

### Phase 7: ternary common-type, compound assignment cross-type, assignment sema ✅
- `tryAnnotateTernaryBranchConversions`: both branches annotated to common type before IR.
- Compound assignment (`+=`, `-=`, etc.) cross-type fix: when `lhsType != commonType`, performs binary op in common type, converts result back to `lhsType`, stores via `Assignment` to original variable.
- Simple `=` RHS converts to `lhsType` (codegen ground truth).
- `UnsignedModulo` opcode added; `%` and `%=` now emit signed vs. unsigned modulo by type.
- `UnsignedDivide`/`UnsignedShiftRight`/`UnsignedModulo` selected in cross-type compound path for unsigned common type.
- `ensureNotInRCX()` helper: prevents shift count from clobbering LHS when result register is RCX (applied to all 5 shift handlers).
- `compoundOpToBaseOpcode()` / `isCompoundAssignmentOp()` centralized in `IROperandHelpers.h`; replaces 4 local maps in `IrGenerator_Expr_Operators.cpp` and the inline list in `SemanticAnalysis.cpp`.
- C++20 [expr.shift] fix: shift operators (`<<`, `>>`, `<<=`, `>>=`) now use independent integral promotions via `tryAnnotateShiftOperandPromotions` instead of usual arithmetic conversions. Codegen uses `promote_integer_type(lhsType)` as the result type for shifts. This prevents `int x <<= long long y` from unnecessarily widening to 64-bit and triggering the cross-type compound assignment path.
- Tests: `test_ternary_conv_ret0`, `test_compound_assign_implicit_cast_ret0`, `test_assign_implicit_cast_ret0`, `test_unsigned_compound_assign_ret0`, `test_unsigned_modulo_ret0`. Suite: 1538 pass / 0 fail / 49 expected-fail.

### Phase 8: constructor arg conversions, enum/pointer contextual bool, literal constant folding ✅ (PR #935)
- `tryAnnotateConstructorCallArgConversions`: annotates `ConstructorCallNode` arguments in expression context with implicit primitive conversions. Calls `adjust_argument_type_for_overload_resolution` and uses `skip_implicit=true` to match the codegen path exactly.
- `tryAnnotateInitListConstructorArgs`: annotates `InitializerListNode` arguments in direct-init variable declarations (`T obj(a, b)`) with the same approach, guarded by `hasAnyConstructor()` to skip template stubs.
- `applyConstructorArgConversion` helper: extracted shared sema-first + standard-conversion-fallback codegen logic used by both `IrGenerator_Visitors_Decl.cpp` and `IrGenerator_Stmt_Decl.cpp`, eliminating the previous duplicate 28-line blocks.
- `tryAnnotateContextualBool` extended: annotates `Type::Enum` and pointer-typed expressions in boolean contexts using `BooleanConversion` and `PointerConversion` kinds respectively. Backend `TEST` instruction already handles these correctly; annotations record semantic intent for future migration.
- `PointerConversion` added to `StandardConversionKind` enum.
- Literal float→int constant folding: `generateTypeConversion` now constant-folds `double` literal → integer at compile time (with assert), fixing a latent crash in `handleFloatToInt` which only supports `TempVar`/`StringHandle` operands. `int` literal → `float/double` still emits `IntToFloat` IR (backend `loadTypedValueIntoRegister` handles it correctly).
- Removed blanket `catch(...)` blocks from both sema functions; proper guards (`hasAnyConstructor()`, null checks, early returns) prevent crashes on incomplete template structs.
- Tests: `test_ctor_call_arg_implicit_cast_ret0`, `test_contextual_bool_enum_ret0`, `test_contextual_bool_pointer_ret0`. Suite: 1548 pass / 0 fail / 52 expected-fail.

### Phase 9: global/static assignment conversion, contextual-bool consumption ✅
- Global/static simple `=` assignment: RHS now converted to LHS type via `generateTypeConversion` (e.g., `double g; g = 42;` correctly emits `IntToFloat`), and the expression result is an lvalue referring to the global/static LHS per C++20 `[expr.ass]/3`.
- Global/static compound assignment (`+=`, `-=`, `*=`, `/=`, etc.): uses `get_common_type()` for usual arithmetic conversions, selects correct float/unsigned opcodes, converts result back to LHS type per C++20 `[expr.ass]/7`. Materializes conversion result via explicit `Assignment` before `GlobalStore` to avoid backend register-tracking gap.
- `applyConditionBoolConversion` now consumes enum/pointer contextual-bool sema annotations: recognizes `BooleanConversion` and `PointerConversion` kinds and returns early (backend TEST already handles zero/null → false, non-zero/non-null → true correctly).
- Tests: `test_global_assign_implicit_cast_ret0`, `test_static_assign_implicit_cast_ret0`, `test_global_compound_assign_cross_type_ret0`. Suite: 1555 pass / 0 fail / 54 expected-fail.

### Phase 10: sema target-type verification + codegen consumption hardening ✅
- `tryGlobalSemaConv` and `tryApplySemaConversion` now accept an optional `expected_target` Type parameter (default `Type::Invalid`). When set, the annotation's target type must match the caller's intended conversion target; otherwise the helper returns `false` and the caller's fallback policy runs. This prevents silent semantic misapplication if a future sema change overwrites a slot with a different target type.
- All call sites updated to pass `expected_target`: global simple `=` passes `gsi.type`, global compound passes `commonType`, binary operator LHS/RHS passes `commonType`. Shift RHS promotion intentionally omits verification (sema annotation is the sole authority for independent integral promotion per C++20 [expr.shift]).
- Local variable simple `=` assignment codegen now consumes sema annotations: prefers the sema-annotated conversion (with target-type verification against `lhsType`) over the local `generateTypeConversion` fallback. Previously this was the only assignment path that did not check sema annotations.
- Ternary operator branch conversions now consume sema annotations: both true and false branch conversions use `getSemaAnnotatedTargetType` to verify the sema annotation matches `common_type` before applying the conversion. Previously only the common-type determination (via `getSemaAnnotatedTargetType`) used sema annotations; the actual branch conversion calls did not.
- Tests: `test_assign_sema_consumption_ret0`, `test_ternary_sema_consumption_ret0`, `test_sema_target_verify_ret0`. Suite: 1560 pass / 0 fail / 55 expected-fail.

### Phase 11: unified `buildConversionPlan` + C++20 promotion rank fix ✅
- `buildConversionPlan(Type, Type)` added to `OverloadResolution.h`: single source of truth returning `ConversionPlan` (rank + kind + validity) for primitive-type conversions. Replaces the previous two-call pattern of `can_convert_type()` + `determineConversionKind()`.
- `ConversionPlan` struct added: combines `ConversionRank` (for overload resolution), `StandardConversionKind` (for semantic annotation), and `is_valid` in one value. Includes `toResult()` for backward-compatible `TypeConversionResult` extraction.
- `can_convert_type(Type, Type)` refactored to delegate to `buildConversionPlan().toResult()`: all 36+ existing callers remain unchanged, no API breakage.
- `tryAnnotateConversion` in `SemanticAnalysis.cpp` now calls `buildConversionPlan` directly (one call instead of `can_convert_type` + `determineConversionKind`).
- `determineConversionKind()` removed from `SemanticAnalysis.cpp`: no longer needed as a separate function.
- C++20 [conv.prom] fix: integral promotion rank now correctly limited to types promoted to exactly `int`/`unsigned int` (rank 3). Previously `short → long`/`long long` was over-approximated as Promotion; now correctly classified as Conversion, matching [conv.prom]/1.
- C++20 [conv.fpprom] fix: floating-point promotion now correctly limited to `float → double`. Previously `float → long double` and `double → long double` were classified as FloatingPromotion via size comparison; now correctly classified as FloatingConversion, matching [conv.fpprom]/1.
- Tests: `test_conversion_plan_unified_ret0`. Suite: 1562 pass / 0 fail / 55 expected-fail.

#### Phase 11 bug fix: enum→double init missing IntToFloat conversion
- Root cause: variable initialization path at `IrGenerator_Stmt_Decl.cpp` had a guard `init_type != Type::Enum` that skipped the conversion block entirely. Since enum→int shares the same bit representation, this was invisible for integer targets but caused silent data corruption for double/float targets (storing raw int bits into a float register, producing garbage).
- Fix: resolve `Type::Enum` to its underlying type (via `gTypeInfo`/`EnumTypeInfo`) before entering the conversion check, so `enum → double` correctly follows the `Int → Double` path and emits `IntToFloat` IR.
- Discovered while investigating test `test_conversion_plan_unified_ret0` returning 251 instead of 0 on Windows CI.
- Tests: updated `test_conversion_plan_unified_ret0` passes. Suite: 1562 pass / 0 fail / 55 expected-fail.

#### Phase 11 feature: local (function-scoped) enum declarations
- Parser: added `"enum"` to the keyword dispatch map in `Parser_Statements.cpp` alongside `struct`/`class`/`union`.
- Codegen: updated `visitEnumDeclarationNode` in `IrGenerator_Visitors_Decl.cpp` to re-insert unscoped enumerator symbols into the codegen-local symbol table. Parser-inserted symbols are popped when the function scope closes during parsing; the codegen visitor re-creates them for identifier lookup.
- Both unscoped `enum` and scoped `enum class` (including with explicit underlying types) work inside function bodies.
- Tests: `test_local_enum_ret0`, `test_local_enum_class_ret0`. Suite: 1564 pass / 0 fail / 55 expected-fail.

### Phase 12: enum→primitive sema annotation + identifier type inference fallback ✅
- `tryAnnotateConversion`: `Type::Enum` removed from `is_non_primitive` lambda so enum source types are now accepted. Separate guard rejects implicit conversion TO enum (C++11+ rule). `buildConversionPlan(Type::Enum, target)` already handles enum→int (IntegralPromotion), enum→other integral (IntegralConversion), enum→float/double (FloatingIntegralConversion), and enum→bool (BooleanConversion).
- `tryAnnotateBinaryOperandConversions`: removed `Type::Enum` rejection; resolves enum operands to their underlying type (via `gTypeInfo[type_index].getEnumInfo()->underlying_type`) before calling `get_common_type`, which only handles primitive integer/floating-point types.
- `tryAnnotateShiftOperandPromotions`: same pattern as binary-op — resolves enum to underlying type before `promote_integer_type`. Per C++20 `[expr.shift]`, unscoped enum operands undergo integral promotion.
- `inferExpressionType`: `IdentifierNode` handler now falls back to `parser_.get_expression_type(node)` when `lookupLocalType` fails. This resolves enumerator constants (e.g., `Red`, `Green`) and other global identifiers not tracked in the sema scope stack.
- `generateTypeConversion`: resolves `Type::Enum` to its underlying integer type at function entry (via `operands.type_index` → `gTypeInfo[...].getEnumInfo()->underlying_type`). This ensures correct signedness for sign/zero extension decisions and correct float/int domain classification. Previously, passing `Type::Enum` as `fromType` could produce incorrect sign extension (because `is_signed_integer_type(Type::Enum)` returns false) or miss float↔int conversion paths.
- Codegen consumers updated: `tryApplySemaConversion` (binary-op), `tryGlobalSemaConv` (global assignment), return-conversion, call-arg-conversion, and variable-init-conversion paths all handle the case where sema annotates `from_type = Type::Enum` but codegen has already resolved the operand to its underlying type (via `tryMakeEnumeratorConstantExpr`). When the mismatch is detected and `from_type == Type::Enum`, the consumer uses `operands.type` (the actual runtime type) for the conversion call.
- Tests: `test_enum_sema_conversion_ret0` (enum→int: return, decl init, assignment, call arg), `test_enum_to_double_sema_ret0` (enum→double: decl init, call arg, return), `test_enum_binop_sema_ret0` (enum in binary arithmetic/comparison). Suite: 1586 pass / 0 fail / 56 expected-fail.

### Phase 13: unary operator integral promotions + scoped enum diagnostic + `inferExpressionType` expansion ✅
- `tryAnnotateUnaryOperandPromotion`: new sema annotation function for C++20 `[expr.unary.op]`. The operand of unary `+`, `-`, and `~` undergoes integral promotion (bool/char/short → int). Resolves enum operands to underlying type before promotion. Sema annotates the operand node with the promoted type.
- Codegen consumption for unary promotions: `generateUnaryOperatorIr` now checks sema annotations on the operand before applying unary `+`, `-`, `~`. Consumes the sema-annotated integral promotion (with enum mismatch handling), then falls back to codegen-local promotion for small integer types. Previously, unary `-` and `~` operated in the operand's original bit width (e.g., 8-bit for `char`), producing incorrect results for cases like `~(unsigned char)0xFF` → 0 instead of the correct -256.
- `inferExpressionType` expansion:
	- `~` operator now returns the promoted type (same integral promotion rules as `+` and `-`).
	- `!` operator now returns `Type::Bool`.
	- `++`/`--` prefix and postfix operators now return the operand type.
	- `SizeofExprNode` and `AlignofExprNode` now return `Type::UnsignedLongLong` (matching codegen's 64-bit `size_t`).
- Scoped enum diagnostic: `diagnoseScopedEnumConversion()` helper throws `CompileError` when an implicit conversion from a scoped enum (`enum class`) to a different type is attempted. Active in variable initialization and return statement contexts. `tryAnnotateConversion` silently rejects scoped enum source types (returning false) so that binary operator comparisons of same-type scoped enums (`Color::Red == Color::Green`) continue to work correctly.
- `normalizeExpression` for `UnaryOperatorNode`: now calls `tryAnnotateUnaryOperandPromotion(e)` for `+`, `-`, `~` operators (previously only handled `!` via `tryAnnotateContextualBool`).
- Tests: `test_unary_promotion_sema_ret0` (unary +/-/~ on char/short/bool/unsigned char with integral promotion), `test_sizeof_alignof_sema_ret0` (sizeof/alignof type inference in expressions), `test_scoped_enum_implicit_conv_fail` (scoped enum → int rejected). Suite: 1588 pass / 0 fail / 57 expected-fail.

### Phase 14: scoped enum diagnostic expansion + `inferExpressionType` coverage ✅
- `diagnoseScopedEnumBinaryOperands`: new diagnostic function for scoped enums used in binary arithmetic, comparison (cross-type), compound assignment, and shift operations. Per C++20, scoped enums only support relational/equality operators between values of the same scoped enum type; all other binary operator usage with a scoped enum operand is ill-formed.
- `diagnoseScopedEnumConversion` extended to assignment RHS context: `int x; x = scoped_val;` now diagnoses with "cannot implicitly convert from scoped enum 'Color' to 'int' in assignment; use static_cast".
- `diagnoseScopedEnumConversion` extended to function call argument context: `foo(scoped_val)` where `foo(int)` now diagnoses with "cannot implicitly convert from scoped enum 'Color' to 'int' in function argument; use static_cast".
- Same-type scoped enum comparisons (`Color::Red == Color::Green`, `a < b`) remain valid as per C++20.
- `inferExpressionType` expanded with 7 new expression type handlers:
	- `ConstructorCallNode`: returns the constructed type (canonicalized from `type_node()`).
	- `StringLiteralNode`: returns `const char*` (pointer to const char).
	- `QualifiedIdentifierNode`: falls back to `parser_.get_expression_type()` for namespace-qualified identifiers.
	- `DynamicCastNode`: returns the target cast type.
	- `OffsetofExprNode`: returns `size_t` (`Type::UnsignedLongLong` on 64-bit).
	- `NoexceptExprNode`: returns `Type::Bool`.
	- `TypeTraitExprNode`: returns `Type::Bool` (all type trait intrinsics return bool).
- Helper functions `isScopedEnum()` and `getScopedEnumName()` extracted as static utilities for scoped enum detection from `CanonicalTypeDesc`.
- Follow-up hot-path cleanup: `normalizeExpression` now computes binary operand `CanonicalTypeId`s once and threads them through `diagnoseScopedEnumBinaryOperands`, `tryAnnotateBinaryOperandConversions`, and `tryAnnotateShiftOperandPromotions` so Phase 14 does not double `inferExpressionType` work for every arithmetic/comparison/compound/shift operator.
- Tests: `test_scoped_enum_assign_fail`, `test_scoped_enum_call_arg_fail`, `test_scoped_enum_compound_assign_fail`, `test_scoped_enum_binop_fail`, `test_infer_expr_type_expansion_ret0`. Suite: 1596 pass / 0 fail / 63 expected-fail.

### Phase 15: remove codegen conversion fallbacks for sema-covered contexts ✅
- **Goal:** For every conversion context where sema already annotates (binary arithmetic, shift, unary promotion, return, call args, constructor args, variable init, assignment, compound assignment, ternary branches): replace the codegen-local `generateTypeConversion` fallback with hard enforcement. Codegen trusts sema exclusively for standard arithmetic type conversions. Any remaining fallback for arithmetic types throws `InternalError`.
- Added `is_standard_arithmetic_type()` helper in `AstNodeTypes_DeclNodes.h`: returns true for integer types, floating-point types, and bool — the types where sema owns implicit conversion annotation.
- Added `hasNormalizedBody()` tracking in `SemanticAnalysis`: sema records which function bodies it normalized; codegen's `IrGenerator` checks this via `sema_normalized_current_function_` before enforcing, correctly excluding template instantiation contexts that sema never visited.
- Added `hasUnresolvedCallArgs()` in `SemanticAnalysis`: tracks `FunctionCallNode` pointers where sema attempted call-arg annotation but couldn't resolve the callee (e.g. template specialization with separate `DeclarationNode` copies). Codegen checks this to suppress hard enforcement for known unresolvable cases.
- **Fallback sites audited and converted (11 total):**
	1. **Function call arguments** (`IrGenerator_Call_Direct.cpp`): sema annotation checked first; `InternalError` for arithmetic types when sema missed (with `hasUnresolvedCallArgs` escape hatch).
	2. **Return value conversion** (`IrGenerator_Visitors_Namespace.cpp`): non-struct return conversions protected with `InternalError`. Same-type size mismatches (e.g., int 64→32 from function-pointer call) excluded since those are size adjustments, not type conversions.
	3. **Variable init** (`IrGenerator_Stmt_Decl.cpp`): sema annotation checked first; `InternalError` for arithmetic types.
	4. **Constructor arguments** (`IrGenerator_Expr_Conversions.cpp`): sema annotation checked first; `InternalError` for arithmetic types.
	5. **Unary promotion** (`IrGenerator_Expr_Conversions.cpp`): sema annotation checked first; `InternalError` for promotable types (bool, small integers). Fallback preserved when `sema_` is null (template instantiation contexts).
	6. **Global/static assignment** (`IrGenerator_Expr_Operators.cpp`): sema annotation checked first; `InternalError` for arithmetic types.
	7. **Compound assignment to global: LHS conversion** (`IrGenerator_Expr_Operators.cpp`): sema annotation checked first; `InternalError` for arithmetic types.
	8. **Compound assignment to global: shift RHS promotion** (`IrGenerator_Expr_Operators.cpp`): now tries sema annotation before falling back (previously was codegen-only); `InternalError` for arithmetic types.
	9. **Compound assignment to global: RHS conversion** (`IrGenerator_Expr_Operators.cpp`): sema annotation checked first; `InternalError` for arithmetic types.
	10. **Local assignment** (`IrGenerator_Expr_Operators.cpp`): sema annotation checked first; `InternalError` for arithmetic types.
	11. **Binary arithmetic LHS/RHS** (`IrGenerator_Expr_Operators.cpp`): sema annotation checked first; `InternalError` for arithmetic types. Shift RHS promotion now also tries sema annotation before falling back; `InternalError` when missed.
- **Contexts that keep unconditional fallback (sema doesn't own these yet):**
	- Struct/user-defined type conversions (user-defined `operator T()`, converting constructors)
	- Enum types (resolved to underlying type by `generateTypeConversion`)
	- Auto, function_pointer, and other non-arithmetic types
	- Same-type size mismatches (size adjustments, not type conversions)
	- Compound assignment result back-conversion (`commonType → lhsType`, C++20 `[expr.ass]/7`)
	- Ternary branch conversions (sema-determined `common_type` used directly, not a fallback pattern)
	- When `sema_` is null (e.g., template instantiation contexts without sema)
	- When `sema_normalized_current_function_` is false (function body not visited by sema, e.g. template instantiation member functions)
- **Sema gaps fixed:**
	- Comma-separated variable declarations (`int x = 3, y = 4;`) — `parse_declaration_or_function_definition` in `Parser_Decl_FunctionOrVar.cpp` (used for built-in type keywords) now sets `is_synthetic_decl_list=true` on the wrapper `BlockNode`, matching `Parser_Statements.cpp`. This ensures all declarators are visible in the enclosing sema scope.
	- `ArraySubscriptNode` handler added to `inferExpressionType` — resolves element type by stripping one array dimension (or pointer level).
	- Struct member function lookup fallback in `tryAnnotateCallArgConversions` — for qualified calls where symbol table lookup fails, searches `gTypesByName` struct member functions using two-pass approach (exact `DeclarationNode` address match first, then name+arity match only when unambiguous).
- **Remaining known limitation:** One test (`test_member_template_func_in_specialization_ret0.cpp`) — template specializations with both primary and specialized struct definitions create separate `DeclarationNode` copies that don't match by address, and the struct name in `gTypesByName` may include template parameters preventing lookup. Documented for Phase 16+.
- Tests: `test_phase15_sema_fallback_removal_ret0`. Suite: 1599 pass / 0 fail / 64 expected-fail.

### Phase 16: constructor/destructor sema coverage + bitwise operator classification ✅
- **Goal:** Extend Phase 15 hard enforcement to constructor and destructor bodies. Add bitwise operator (`&`, `|`, `^`) classification to semantic normalization for proper scoped enum diagnostics and usual arithmetic conversion annotations.
- **Constructor/destructor sema normalization fix:**
	- Added `normalizeConstructorDeclaration()` and `normalizeDestructorDeclaration()` in `SemanticAnalysis.cpp`, mirroring `normalizeFunctionDeclaration()` with proper `pushScope()` / parameter registration / `popScope()` scope management.
	- Previously, constructor bodies were normalized with a plain `SemanticContext` and no parameter scope — constructor parameters were invisible to `lookupLocalType()`, causing type inference failures and missed sema annotations.
	- Refactored `normalizeTopLevelNode()` and `normalizeStructDeclaration()` to use these new helpers, eliminating duplicated inline normalization code.
	- Extracted shared `registerParametersInScope()` helper used by both `normalizeFunctionDeclaration` and `normalizeConstructorDeclaration`, eliminating code duplication.
	- `normalizeConstructorDeclaration` now walks member initializer expressions (`MemberInitializer::initializer_expr`), base initializer arguments (`BaseInitializer::arguments`), and delegating constructor arguments (`DelegatingInitializer::arguments`) through `normalizeExpression()`. This ensures arithmetic conversions in expressions like `Foo(short x) : result(x + 1) {}` are sema-annotated, preventing Phase 15 `InternalError`.
	- Added forward declarations for `ConstructorDeclarationNode` and `DestructorDeclarationNode` in `SemanticAnalysis.h` for consistency with other node types.
- **Constructor/destructor codegen flag:**
	- `visitConstructorDeclarationNode()` and `visitDestructorDeclarationNode()` in `IrGenerator_Visitors_Decl.cpp` now set `sema_normalized_current_function_` using `hasNormalizedBody()`, matching the pattern in `visitFunctionDeclarationNode()`.
	- Phase 15 hard enforcement now covers constructor and destructor bodies when sema has normalized them.
- **Bitwise operator sema classification:**
	- Added `is_bitwise` classification for `&`, `|`, `^` in `normalizeExpression` (C++20 `[expr.bit.and]`, `[expr.bit.or]`, `[expr.bit.xor]`).
	- `is_bitwise` is included in `needs_binary_type_inference`, triggering `diagnoseScopedEnumBinaryOperands()` for scoped enum operand diagnosis.
	- `is_bitwise` operators use `tryAnnotateBinaryOperandConversions()` for usual arithmetic conversion annotations (same as `is_arithmetic`).
- Tests: `test_scoped_enum_bitwise_fail`, `test_ctor_sema_conversion_ret0`, `test_bitwise_sema_conversion_ret0`, `test_ctor_member_init_expr_ret0`. Suite: 1609 pass / 0 fail / 65 expected-fail.

### Phase 17: compound assignment back-conversion sema ownership + template callee resolution + inferExpressionType expansion + scoped enum ctor diagnostics ✅
- **Goal:** Address the highest-impact remaining known limitations from Phase 16.
- **Compound assignment result back-conversion sema ownership (C++20 [expr.ass]/7):**
	- Added `tryAnnotateCompoundAssignBackConversion()` method that annotates the `commonType → lhsType` back-conversion on the `BinaryOperatorNode` itself (keyed in `compound_assign_back_conv_` side table).
	- Covers both regular compound assignment operators (`+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`) and shift compound assignment operators (`<<=`, `>>=`).
	- For shifts, the back-conversion is from `promote_integer_type(lhsType)` to `lhsType` (independent integral promotion, not usual arithmetic conversions).
	- Added `getCompoundAssignBackConv()` public accessor for codegen verification.
	- Codegen: both local and global compound assignment paths now verify sema annotation with `InternalError` if missing for standard arithmetic types.
- **Template specialization callee resolution:**
	- Refactored `tryAnnotateCallArgConversions` struct member function search into a reusable lambda (`searchStructMembers`) with a 3-pass strategy: (1) exact `DeclarationNode` address match, (2) mangled name match via `FunctionCallNode::mangled_name()` / `FunctionDeclarationNode::mangled_name()`, (3) unambiguous name + arity match.
	- Added fallback scan of `gTypesByName` when direct struct name lookup fails, matching entries whose name ends with the struct name fragment (handles namespace prefix differences and template-argument-decorated keys). This is an O(n) linear scan over all registered types and should be removed once type registration keys are consistent (see `docs/TYPE_LOOKUP_OPTIMIZATION_PLAN.md` Phase 2).
	- Reduces reliance on the `hasUnresolvedCallArgs` escape hatch for template specialization callee resolution.
- **Expand `inferExpressionType` coverage:**
	- `NewExpressionNode`: returns pointer to allocated type (wraps the new'd type in a `PointerLevel`).
	- `DeleteExpressionNode`: returns `Type::Void` per C++20 `[expr.delete]`.
	- `LambdaExpressionNode`: looks up the generated `__lambda_N` struct in `gTypesByName`; falls back to `parser_.get_expression_type()` if not yet generated.
	- `ThrowExpressionNode`: returns `Type::Void` per C++20 `[expr.throw]`.
- **Scoped enum constructor argument diagnostics:**
	- Added `diagnoseScopedEnumConversion()` in `tryAnnotateConstructorCallArgConversions` (for `ConstructorCallNode` expression contexts).
	- Added `diagnoseScopedEnumConversion()` in `tryAnnotateInitListConstructorArgs` (for direct-init syntax like `Struct s(scoped_enum_var)`) — fires after overload resolution when the parser can resolve the argument type.
	- When `parser_.get_expression_type()` returns `nullopt` in the init-list path, the method bails out without diagnostics (cannot determine target parameter type). This is safe: the scoped enum diagnostic still fires in expression-syntax constructor calls via `tryAnnotateConstructorCallArgConversions`.
- Tests: `test_compound_assign_back_conv_sema_ret0`, `test_new_expr_type_inference_ret0`, `test_scoped_enum_ctor_arg_fail`, `test_scoped_enum_ctor_same_type_ret0`. Suite: 1615 pass / 0 fail / 66 expected-fail.

### Phase 18 ✅: unified `resolve_constructor` + remove codegen constructor matching

**Goal:** Extract a single `resolve_constructor_overload()` function in `OverloadResolution.h` and remove all hand-rolled constructor matching loops from codegen, constexpr evaluator, and sema.

**Implementation (completed):**

- Added shared constructor-resolution helpers in `src/OverloadResolution.h`:
  - `resolve_constructor_overload()` for type-ranked matching via `can_convert_type()`
  - `resolve_constructor_overload_arity()` for arity-only fallback when argument types are unavailable
- Both shared helpers honor default-argument viability, and the copy/move skip path now filters only compiler-generated copy/move candidates instead of all implicit constructors.
- The type-inference gap in `tryAnnotateInitListConstructorArgs` was fixed: when `parser_.get_expression_type()` returns `nullopt` for an argument (for example a scoped enum value like `Color::Red`), sema now falls back to `inferExpressionType()` + `materializeTypeSpecifier()` so valid direct-init constructor calls still reach normal overload resolution.
- Codegen/IR/constexpr constructor selection now flows through the shared helpers instead of repeating open-coded constructor loops.
- Regression coverage includes `tests/test_ctor_default_arg_overload_ret0.cpp` and `tests/test_global_ctor_default_arg_ret0.cpp`.

**Problem statement:**

Constructor overload resolution is currently implemented independently in at least **9 separate sites** across 4 files:

| File | Sites | Default arg handling | Type matching |
|------|-------|---------------------|---------------|
| `CodeGen.h` | 2 (lines ~5703, ~6318) | ✅ `has_default_value()` loop | ❌ arity only |
| `ConstExprEvaluator.h` | 5 (lines ~2882, ~2963, ~3168, ~3603, ~3773) | ❌ strict `params.size() == args.size()` | ❌ arity only |
| `IRConverter.h` | 1 (line ~6772) | ❌ arity only | ❌ arity only |
| `SemanticAnalysis.cpp` | 1 (`tryAnnotateInitListConstructorArgs` fallback, line ~2522) | ⚠️ `params.size() < initializers.size()` (no `has_default_value` check) | ✅ checks `canonical_types_match` at `arg_idx` |

Every site re-implements the same pattern: iterate `struct_info.member_functions`, filter `is_constructor`, compare parameter counts, optionally check default arguments. The codegen sites at `CodeGen.h:5784-5804` and `CodeGen.h:6327-6341` are the most complete (they verify `has_default_value()` on surplus parameters), but even those don't do type-based overload resolution — they match by arity alone and take the first match.

The sema fallback at `SemanticAnalysis.cpp:2522-2538` is the newest copy and was added in Phase 17 specifically because `parser_.get_expression_type()` returns `nullopt` for scoped enum variables in init-list contexts, preventing the normal `resolve_constructor_overload` path (line 2555) from running. This is a workaround for a type-inference gap, not a principled design.

**Plan:**

1. **Add `resolve_constructor_overload()` to `OverloadResolution.h`:**
	- Accepts `const StructTypeInfo&` + argument types (as `std::vector<TypeSpecifierNode>` for type-based resolution, with an arity-only fallback when types are unavailable).
	- Handles default arguments: a constructor is viable when `min_required_args <= num_args <= params.size()`, where `min_required_args` counts parameters without `has_default_value()`.
	- Handles copy/move constructor skipping for brace-init contexts (currently inline in `CodeGen.h:5720-5774`).
	- Returns `OverloadResolutionResult` with the selected `ConstructorDeclarationNode*`, consistent with the existing `resolve_overload` API.
	- The `skip_implicit` parameter (already used by sema's `tryAnnotateConstructorCallArgConversions`) should be part of this API.

2. **Fix `tryAnnotateInitListConstructorArgs` type-inference gap:**
	- When `parser_.get_expression_type()` returns `nullopt`, fall back to `inferExpressionType()` + `materializeTypeSpecifier()` to build the `TypeSpecifierNode` for that argument.
	- This eliminates the need for the hand-rolled scoped-enum-specific constructor matching loop at lines 2507-2548 entirely — the normal `resolve_constructor_overload` path (line 2555) can run, and `diagnoseScopedEnumConversion` (line 2574) handles the diagnostic.
	- This also fixes the default-argument false-positive bug without needing a separate patch.

3. **Replace codegen constructor matching in `CodeGen.h`:**
	- Both sites (~5703 and ~6318) should call `resolve_constructor_overload()` instead of inline loops.
	- The brace-init copy/move constructor skip logic (~5720-5774) moves into the shared function.
	- Default argument fill-in (~5892-5910) stays in codegen (it generates IR for default value expressions), but the *selection* of which constructor to use moves to the shared resolver.

4. **Replace constexpr evaluator constructor matching in `ConstExprEvaluator.h`:**
	- All 5 sites (~2882, ~2963, ~3168, ~3603, ~3773) should call `resolve_constructor_overload()`.
	- These currently don't handle default arguments at all (`params.size() == args.size()` strict equality), so this is also a bug fix — constexpr evaluation of constructors with default arguments likely fails silently today.

5. **Replace IRConverter constructor matching:**
	- The site at `IRConverter.h:~6772` should also use the shared resolver.

**Expected outcome:**
- One canonical constructor resolution function, shared by sema, codegen, constexpr evaluator, and IR converter.
- Default argument handling is correct everywhere (not just in 2 of 9 sites).
- Type-based overload resolution for constructors (not just arity matching).
- The scoped enum init-list diagnostic fallback in sema is eliminated — the normal overload resolution path handles it.
- Easier to add future constructor-related features (explicit constructors, deleted constructors, inherited constructors) in one place.

**Files to modify:**
- `src/OverloadResolution.h` — add `resolve_constructor_overload()`
- `src/SemanticAnalysis.cpp` — remove hand-rolled fallback; use `inferExpressionType` + `materializeTypeSpecifier` when parser can't resolve arg type
- `src/CodeGen.h` — replace 2 inline constructor matching loops
- `src/ConstExprEvaluator.h` — replace 5 inline constructor matching loops
- `src/IRConverter.h` — replace 1 inline constructor matching loop

### Post-Phase-18 cleanup ✅: unified `findSameTypeConstructorCore`

**Goal:** Replace the three separate `StructTypeInfo` helper functions (`findCopyConstructor`, `findMoveConstructor`, `findPreferredSameTypeConstructor`) — and the per-function iteration loops they contained — with a single shared core helper that all three delegate to.

**Bugs fixed:**
1. `findCopyConstructor()` used `is_reference()` which returns `true` for both lvalue *and* rvalue references, potentially matching a move constructor as a "copy" constructor. The unified helper uses `is_lvalue_reference()` for copy and `is_rvalue_reference()` for move.
2. None of the three finder functions accepted constructors with default arguments (e.g. `Foo(const Foo&, int = 0)` has `params.size() == 2` but is a valid copy constructor per C++20 [class.copy.ctor]/1). The unified helper computes minimum required args and accepts any constructor where only the first parameter is required.

**Implementation:**
- Added `StructTypeInfo::findSameTypeConstructorCore(bool want_move, bool include_implicit)` in `src/AstNodeTypes.cpp` as the single iteration loop.
- `findCopyConstructor()` → delegates to `findSameTypeConstructorCore(false, false)`.
- `findMoveConstructor()` → delegates to `findSameTypeConstructorCore(true, false)`.
- `findPreferredSameTypeConstructor()` → calls `findSameTypeConstructorCore()` twice (move then copy) with deletion-flag guards, preserving its existing fallback semantics.
- All existing callers (`IRConverter_ConvertMain.cpp`, `IrGenerator_Stmt_Decl.cpp`, `IrGenerator_Visitors_Decl.cpp`, `IrGenerator_MemberAccess.cpp`) are unchanged — they call the same public API which now routes through the shared core.
- Declaration updated in `src/AstNodeTypes_DeclNodes.h`.

**Regression tests added:**
- `tests/test_copy_ctor_default_arg_ret0.cpp` — copy constructor with trailing default argument.
- `tests/test_copy_move_ctor_select_ret0.cpp` — lvalue correctly selects copy constructor.
- `tests/test_implicit_copy_ctor_ret0.cpp` — implicit (compiler-generated) copy on POD struct.

**Suite:** 1626 pass / 0 fail / 67 expected-fail.

### Post-Phase-18 cleanup (continued): ad-hoc copy/move ctor detection audit

**Goal:** Fix the same two bugs (is_reference() matching rvalue refs, params.size()==1 rejecting default-arg ctors) in ad-hoc inline detection loops outside `StructTypeInfo`, and fix downstream assumptions about single-param constructors.

**Additional sites fixed:**
1. `src/Parser_Decl_StructEnum.cpp:2758-2766` — copy/move ctor classification during struct parsing: replaced `is_reference()` with `is_lvalue_reference()` and relaxed `params.size()==1` to compute min-required-args.
2. `src/Parser_Decl_StructEnum.cpp:2884-2891` — inherited ctor filtering (skip copy/move ctors): same fixes, now handles default-arg ctors.
3. `src/Parser_Decl_StructEnum.cpp:1727-1748` — deleted ctor detection: replaced `is_reference()` with `is_lvalue_reference()`, relaxed `num_params==1`.
4. `src/Parser_Decl_StructEnum.cpp:2153-2182` — deleted assignment operator detection: relaxed `params.size()==1` to `!params.empty()`.
5. `src/Parser_Decl_StructEnum.cpp:2795-2802` — assignment operator refinement (CopyAssign/MoveAssign): replaced `is_reference() && !is_rvalue_reference()` with `is_lvalue_reference()`.
6. `src/IrGenerator_Visitors_Decl.cpp:1349-1351` — unnamed param "other" naming: relaxed `parameter_nodes().size()==1` to min-required-args, replaced `is_reference()` with `is_lvalue_reference()`.
7. `src/IrGenerator_Visitors_Decl.cpp:1557-1567` — implicit copy/move ctor detection: replaced `is_reference()` + nested `is_rvalue_reference()` with separate `is_lvalue_reference()`/`is_rvalue_reference()` checks.
8. `src/IRConverter_ConvertMain.cpp:4503` — `emitSameTypeCopyOrMoveConstructorCall`: added early return false for multi-param ctors so they fall through to handleConstructorCall.
9. `src/IRConverter_ConvertMain.cpp:4859` — CV qualifier extraction: relaxed `params.size()==1` to `!params.empty()` since findCopyConstructor can return multi-param ctors.
10. `src/IRConverter_ConvertMain.cpp:4739` — handleConstructorCall `num_params==1` guard: added explanatory comment that fillInConstructorDefaultArguments expands defaults before this point.
11. `src/IrGenerator_Stmt_Decl.cpp:2038` — copy-initialization path: added `fillInConstructorDefaultArguments` call for multi-param copy ctors (was missing, causing linker errors for `Foo(const Foo&, int=0)`).
12. `src/OverloadResolution.h:710` — `isImplicitCopyOrMoveConstructorCandidate`: replaced `is_reference() || is_rvalue_reference()` with `is_lvalue_reference() || is_rvalue_reference()`.
13. `src/AstNodeTypes.cpp:848` — `findCopyAssignmentOperator` slow path: replaced `is_reference() && !is_rvalue_reference()` with `is_lvalue_reference()`.

**Regression tests added:**
- `tests/test_copy_move_brace_init_ret0.cpp` — struct with both copy and move ctors, copy from lvalue correctly selects copy ctor.
- `tests/test_copy_ctor_default_arg_inherited_ret0.cpp` — copy ctor with default args works across copy-initialization path.

**Suite:** 1628 pass / 0 fail / 67 expected-fail.

### Post-Phase-18 cleanup (continued): own-type check, computeMinRequiredArgs, throw/catch

**Goal:** Fix two correctness bugs introduced by the earlier `min_required <= 1` relaxation, and tighten the deleted assignment operator detection.

**Bugs fixed:**
1. `Parser_Decl_StructEnum.cpp:2768-2771` — copy/move ctor classification: added `param_type.type_index() == struct_type_info.type_index_` own-type check. Without this, a constructor like `Foo(const Bar&, int=0)` with `min_required==1` was incorrectly classified as a copy constructor (suppressing implicit copy ctor generation).
2. `Parser_Decl_StructEnum.cpp:2898-2902` — inherited ctor filtering: added `base_struct_info->isOwnTypeIndex(param_type.type_index())` check. Without this, a base-class converting ctor like `Base(const SomeConfig&, int=0)` was wrongly skipped during `using Base::Base;` constructor inheritance.
3. `Parser_Decl_StructEnum.cpp:2155` — deleted assignment operator detection: changed `!params.empty()` to `!params.empty() && computeMinRequiredArgs(params) <= 1`, so multi-required-arg operators (like `operator=(const Foo&, const Bar&)`) are not falsely detected as copy/move assignments.

**Regression test added:**
- `tests/test_throw_catch_default_arg_copy_ctor_ret0.cpp` — throw/catch cycle with a copy ctor that has a trailing default argument, exercising the `emitSameTypeCopyOrMoveConstructorCall` early-return path.

**Suite:** 1629 pass / 0 fail / 67 expected-fail.

### Phase 19 ✅: `buildConversionPlan` extended to `TypeSpecifierNode`-level conversions

**Goal:** Extend `buildConversionPlan` from primitive `Type` values to full `TypeSpecifierNode`-level conversions, covering pointers, references, struct type-index matching, derived-to-base, user-defined conversions, and type aliases. Refactor `can_convert_type(const TypeSpecifierNode&, const TypeSpecifierNode&)` to delegate to the new overload via `.toResult()`, matching the pattern the primitive overload already uses.

**Implementation:**
- Added `buildConversionPlan(const TypeSpecifierNode&, const TypeSpecifierNode&)` overload in `src/OverloadResolution.h`: returns `ConversionPlan` (rank + `StandardConversionKind` + validity) for all TypeSpecifierNode-level conversion cases.
- `can_convert_type(const TypeSpecifierNode&, const TypeSpecifierNode&)` refactored to a one-liner: `return buildConversionPlan(from, to).toResult();` — all existing callers remain unchanged, no API breakage.
- Conversion kinds assigned to previously untyped conversion paths:
  - Pointer `T* → const T*`: `QualificationAdjustment`
  - Pointer `T* → void*` / `const T* → const void*`: `PointerConversion`
  - Unresolved `UserDefined` pointer types: `PointerConversion`
  - Derived-to-base reference binding: `DerivedToBase`
  - Struct-to-primitive / primitive-to-struct / struct-to-struct converting constructors: `UserDefined`
  - Unresolved type alias conversions: `None` (kind indeterminate at parse time)
- Internal calls from `can_convert_type(Type, Type)` replaced with `buildConversionPlan(Type, Type)` to return full plans instead of discarding the `StandardConversionKind`.
- No new `StandardConversionKind` values needed — all existing enum values in `src/SemanticTypes.h` are sufficient.
- `ConversionRank` values remain identical — no overload resolution behavior change.

**Suite:** 1633 pass / 0 fail / 67 expected-fail.

### Follow-up slice ✅: deleted special-member diagnostics in current IR-side paths

**Goal:** Reduce the deleted copy/move special-member gap without waiting for the
full semantic-normalization architecture. This slice keeps the fix behavior-safe
and local to existing codegen-time validation points.

**Implementation:**
- `src/IrGenerator_Stmt_Decl.cpp`
	- same-type direct-init / brace-init lvalue paths now reject deleted copy
	  constructor use before constructor lookup can silently fall through
	- same-type xvalue copy-initialization now rejects deleted move constructor
	  use before the current rvalue fast path can bit-copy the object
	- same-type copy-initialization default-argument fill-in now prefers the move
	  constructor for xvalue sources, but still skips prvalue-elision-sensitive
	  diagnostics
- `src/IrGenerator_Expr_Operators.cpp`
	- same-type direct variable assignment now rejects deleted copy/move
	  assignment both on the raw-assignment fallback path and when the selected
	  `operator=` overload corresponds to the deleted special member

**Regression tests added:**
- `tests/test_deleted_copy_ctor_direct_fail.cpp`
- `tests/test_deleted_copy_ctor_copy_init_fail.cpp`
- `tests/test_deleted_copy_ctor_brace_init_fail.cpp`
- `tests/test_deleted_move_ctor_copy_init_fail.cpp`
- `tests/test_deleted_copy_assignment_fail.cpp`
- `tests/test_deleted_move_assignment_fail.cpp`

**Windows validation:**
- `.\build_flashcpp.bat`
- `.\tests\run_all_tests.ps1 test_copy_move_ctor_select_ret0.cpp test_deleted_special_members_ret0.cpp test_deleted_copy_ctor_brace_init_fail.cpp test_deleted_copy_ctor_copy_init_fail.cpp test_deleted_copy_ctor_direct_fail.cpp test_deleted_copy_assignment_fail.cpp test_deleted_move_assignment_fail.cpp test_deleted_move_ctor_copy_init_fail.cpp`

### Follow-up slice ✅: same-type xvalue direct-init / brace-init deleted move diagnostics

**Goal:** Close the remaining deleted move-constructor gap for same-type xvalue
sources in direct-init and brace-init without broadening the earlier IR-side
special-member work.

**Implementation:**
- `src/IrGenerator_Stmt_Decl.cpp`
	- added `getSameTypeConstructorPreference(...)` to ask the parser whether an
	  initializer expression is same-type and whether it prefers copy vs move
	- direct-init, brace-init, and the constructor-call path now reuse that
	  preference so expressions like `static_cast<T&&>(x)` diagnose deleted move
	  constructors instead of falling back to identifier-only copy checks
	- `isSameTypeXValueSource(...)` now reuses the parser-derived preference
	  before falling back to runtime metadata

**Regression tests added:**
- `tests/test_deleted_move_ctor_direct_init_xvalue_fail.cpp`
- `tests/test_deleted_move_ctor_brace_init_xvalue_fail.cpp`

**Windows validation:**
- `.\build_flashcpp.bat`
- `.\tests\run_all_tests.ps1 test_deleted_move_ctor_copy_init_fail.cpp test_deleted_move_ctor_direct_init_xvalue_fail.cpp test_deleted_move_ctor_brace_init_xvalue_fail.cpp`

### Follow-up slice ✅: deleted assignment diagnostics in lvalue-metadata store paths

**Goal:** Preserve deleted same-type copy/move assignment diagnostics when
`handleLValueAssignment` takes the fast path for member, array-element, and
indirection stores.

**Implementation:**
- `src/IrGenerator_Expr_Operators.cpp`
	- added `tryResolveExprTypeIndex(...)` so metadata-driven lvalue stores can
	  recover struct `TypeIndex` information from expression results or lvalue
	  metadata when the direct `ExprResult` no longer carries it
	- `handleLValueAssignment(...)` now calls
	  `diagnoseDeletedSameTypeAssignmentUsage(...)` before emitting metadata-path
	  stores for `Member`, `ArrayElement`, and `Indirect` lvalues

**Regression tests added:**
- `tests/test_deleted_copy_assignment_member_fail.cpp`
- `tests/test_deleted_copy_assignment_array_element_fail.cpp`
- `tests/test_deleted_copy_assignment_indirect_fail.cpp`
- `tests/test_deleted_move_assignment_member_fail.cpp`
- `tests/test_deleted_move_assignment_array_element_fail.cpp`
- `tests/test_deleted_move_assignment_indirect_fail.cpp`

**Windows validation:**
- `.\build_flashcpp.bat`
- `.\tests\run_all_tests.ps1 test_deleted_copy_assignment_member_fail.cpp test_deleted_copy_assignment_array_element_fail.cpp test_deleted_copy_assignment_indirect_fail.cpp test_deleted_move_assignment_member_fail.cpp test_deleted_move_assignment_array_element_fail.cpp test_deleted_move_assignment_indirect_fail.cpp test_deleted_copy_assignment_fail.cpp test_deleted_move_assignment_fail.cpp`

### Follow-up slice ✅: deleted copy-assignment fallback for xvalue same-type assignment

**Goal:** Preserve deleted copy-assignment diagnostics when the RHS is an xvalue
but the class has no move assignment operator, so overload resolution falls back
to the copy assignment operator.

**Implementation:**
- `src/AstNodeTypes.cpp`
	- `findCopyAssignmentOperator(...)` and `findMoveAssignmentOperator(...)`
	  now optionally include implicitly generated special members so callers can
	  ask the same “does a move assignment actually exist?” question that the
	  assignment selection path answers
- `src/IrGenerator_Expr_Operators.cpp`
	- `diagnoseDeletedSameTypeAssignmentUsage(...)` now only returns early for
	  xvalue assignments when a move assignment operator really exists; otherwise
	  it falls through to the deleted copy-assignment check

**Regression tests added:**
- `tests/test_deleted_copy_assignment_xvalue_fallback_fail.cpp`

**Windows validation:**
- `.\build_flashcpp.bat`
- `.\tests\run_all_tests.ps1 test_deleted_copy_assignment_fail.cpp test_deleted_copy_assignment_xvalue_fallback_fail.cpp test_deleted_move_assignment_fail.cpp`

### Follow-up slice ✅: deleted copy-constructor fallback for xvalue same-type initialization

**Goal:** Preserve deleted copy-constructor diagnostics when the initializer is
an xvalue but the class has no move constructor, so same-type initialization
falls back to the copy constructor.

**Implementation:**
- `src/AstNodeTypes.cpp`
	- `findCopyConstructor(...)` and `findMoveConstructor(...)` now optionally
	  include implicitly generated special members so callers can ask whether a
	  move constructor really exists before short-circuiting xvalue diagnostics
- `src/IrGenerator_Stmt_Decl.cpp`
	- `diagnoseDeletedSameTypeConstructorUsage(...)` now only returns early for
	  xvalue same-type initialization when a move constructor really exists;
	  otherwise it falls through to the deleted copy-constructor check

**Regression tests added:**
- `tests/test_deleted_copy_ctor_xvalue_fallback_fail.cpp`

**Windows validation:**
- `.\build_flashcpp.bat`
- `.\tests\run_all_tests.ps1 test_deleted_copy_ctor_copy_init_fail.cpp test_deleted_copy_ctor_xvalue_fallback_fail.cpp test_deleted_move_ctor_copy_init_fail.cpp`

### Follow-up slice ✅: global/static compound-assignment result lvalues

**Goal:** Make global and static-local compound assignment expressions return an
lvalue referring to the left operand, matching the simple-assignment fix and the
required `E1 op= E2` result category.

**Implementation:**
- `src/IrGenerator_Expr_Operators.cpp`
	- the global/static compound-assignment path now reuses
	  `makeGlobalAssignmentResultLValue(...)` after `GlobalStore`, so the final
	  expression result carries global lvalue metadata instead of exposing the
	  transient store temp as a prvalue-ish result

**Regression tests added:**
- `tests/test_global_compound_assign_result_lvalue_ret0.cpp`

**Windows validation:**
- `.\build_flashcpp.bat`
- `.\tests\run_all_tests.ps1 test_global_assign_result_lvalue_ret0.cpp test_global_compound_assign_result_lvalue_ret0.cpp test_compound_assign_global_ret42.cpp`

### Follow-up slice ✅: inferExpressionType for template-parameter references and pointer-to-member access

**Goal:** Close the remaining low-risk `inferExpressionType` gaps that already had
clear local sources of truth.

**Implementation:**
- `src/SemanticAnalysis.cpp`
	- `TemplateParameterReferenceNode` now resolves through `lookupLocalType(...)`
	  first and falls back to `parser_.get_expression_type(...)` if the template
	  parameter is not present in the sema scope stack
	- `PointerToMemberAccessNode` now forwards the inferred type of its
	  `member_pointer()` operand, matching the existing IR-generation expectation

**Windows validation:**
- `.\build_flashcpp.bat`
- `.\tests\run_all_tests.ps1 template_nontype_ret10.cpp test_template_type_param_functional_cast_ret0.cpp test_pointer_to_member_comprehensive_ret0.cpp test_ptr_to_member_type_alias_ret42.cpp`

### Known limitations (current, as of Phase 19)

- User-defined `operator bool()` / converting constructors remain in codegen.
- Reference binding, temporary materialization, lifetime extension remain in codegen.
- Integer → bool contextual-bool sema annotations consumed but no explicit IR emitted (backend TEST handles correctly; annotation documents semantic intent only).
- `inferExpressionType` parser fallback (`parser_.get_expression_type`) may be slower than direct scope-stack lookup for hot paths; profiling should verify this is not a bottleneck for large translation units.
- `inferExpressionType` still does not handle: `FoldExpressionNode`, `PackExpansionExprNode`. These return invalid and fall back to parser type resolution or no annotation.

### Parallel rollout guidance

This plan is a good candidate to run partially in parallel with fleet work, but only if the work is split by **infrastructure ownership** versus **language-policy ownership**.

#### Good parallel work

These are relatively safe to do in parallel because they mainly add seams, storage, and observability:

- `main.cpp` pass seam and timing hooks
- `SemanticAnalysis.*` scaffolding with no-op traversal
- packed semantic-slot layout and side-storage helpers
- `CanonicalTypeId` / `TypeContext` interning infrastructure
- debug-printing, counters, and performance instrumentation
- tests and docs for expected semantic-pass behavior

These tasks should still avoid unnecessary overlap in the exact same files, but they are much less likely to create semantic-policy conflicts.

#### Bad parallel overlap

These areas should not be edited independently by multiple streams at the same time, because they encode the actual C++20 language-policy decisions:

- overload resolution ranking/viability
- parser-side `auto` / lambda deduction paths
- `SymbolTable` signature lookup behavior
- function-call conversion policy in `IrGenerator_Call_Direct.cpp`
- arithmetic/comparison conversion policy in `IrGenerator_Expr_Operators.cpp`
- return-conversion policy in `IrGenerator_Visitors_Namespace.cpp`
- any shared logic that decides canonical type identity or constraint satisfaction

If two streams change these areas concurrently, the likely failure mode is not just merge pain; it is semantic drift where ranking, rewriting, diagnostics, and lowering stop agreeing with each other.

#### Practical coordination rule

Recommended split:

- run **Phase 1** and the infrastructure-heavy part of **Phase 2** in parallel with fleet work
- serialize the policy-heavy part of **Phase 2** onward behind a clear owner or short-lived branch window

In practice that means:

- safe parallel target:
	- semantic seam
	- slot/interner infrastructure
	- timing/metrics
	- no-op validation hooks
- coordinated/serialized target:
	- overload viability
	- conversion ranking
	- parser deduction policy
	- codegen policy removal in migrated contexts
	- canonical signature/constraint behavior

#### Operational safeguard

If this is developed in parallel, keep the new semantic path behind a feature flag or tightly scoped rollout switch until a whole migrated context is owned end-to-end.

That avoids the most dangerous transitional state: semantic analysis annotates one policy, while parser/codegen still enforce a different one for the same source construct.

## Testing and validation plan

### Existing behavior to re-verify

- implicit argument conversions
- return conversions
- arithmetic promotions/conversions
- enum conversions and enum identity
- generic lambda tests already mentioned in repo docs
- ordinary `auto` variable deduction
- function `auto` return deduction
- lambda `auto` return deduction
- generic lambda parameter deduction
- `decltype(auto)` once added to the semantic cleanup path

### New tests to budget

- contextual bool conversions
- assignment conversion
- initializer conversion
- reference binding / temporary materialization
- ambiguous / no-match overload diagnostics after canonical type comparison cleanup
- `auto&` / `const auto&` / `auto&&` variable deduction
- ordinary function `auto` return with multiple return paths
- non-generic lambda `auto` return consistency
- generic lambda parameter deduction preserving signedness / width / refs
- `decltype(auto)` preserving reference and value category

### Performance validation

Measure at minimum:

- parse time
- semantic-analysis time
- IR generation time
- AST node count before and after semantic rewriting
- number of expressions carrying non-empty semantic slots
- number of structural semantic nodes allocated (should stay near zero in the first slice)

Reject designs that cause translation-unit-wide AST cloning when only a small fraction of expressions need normalization.

## Important design decisions to carry into implementation

1. **Do build a semantic pass.**
2. **Do not start with a full standalone semantic subsystem.**
3. **Do not mutate the whole AST in place.**
4. **Do reuse `ExpressionSubstitutor`-style selective cloning in the chunk arena.**
5. **Do move codegen policy into semantic normalization incrementally.**
6. **Do treat `auto`/generic-lambda cleanup as a later dedicated phase.**

## Notes

- The existing `docs/2026-03-12_IMPLICIT_CAST_SEMA_PLAN.md` is directionally good, but the implementation plan should explicitly recommend a smaller first milestone: semantic normalization over the existing parser/type system, not a broad parser/semantic split rewrite.
- If the project later wants a fuller semantic layer, Phase 4 will provide the first stable foundation for that by introducing canonical type identity and reducing syntax-node semantic heuristics.

## Expanded design appendix

### A. Recommended ownership model for rewritten roots

There are two practical ways to thread rewritten AST roots into the pipeline:

#### Option 1: mutate parser-owned top-level roots

- add `Parser::mutable_nodes()`
- semantic pass rewrites `parser.ast_nodes_` entries directly
- `main.cpp` keeps reading from `parser->get_nodes()`

Pros:

- smallest pipeline diff in `main.cpp`

Cons:

- increases coupling between `SemanticAnalysis` and `Parser`
- encourages later semantic passes to reach into parser internals

#### Option 2: semantic pass owns normalized roots

- parser remains the owner of syntax AST
- `SemanticAnalysis` produces `std::vector<ASTNode> normalized_roots_`
- `main.cpp` iterates normalized roots instead of raw parser roots
- `AstToIr` still receives `Parser&` for all existing parser services

Pros:

- cleaner separation between syntax and normalized AST
- avoids adding mutable parser internals just for this phase
- makes it easier later to compare raw vs normalized roots during debugging

Cons:

- one additional top-level root vector

### Recommendation

For the **annotation-first first slice**, prefer **Option 1**:

- parser-owned roots stay in place
- semantic pass mutates semantic slots in place
- `main.cpp` can remain almost unchanged

Revisit **Option 2** only once structural semantic rewrites become common enough that a distinct normalized-root view provides real value.

### A1. Parser-inserted null-cast node vs empty semantic slot

Your suggestion maps to two different implementation shapes:

#### Option A: parser inserts a dedicated `NullImplicitCastNode` wrapper

- parser wraps every expression in a semantic placeholder node
- semantic pass later fills in cast kind / target type

Why this is **not** the preferred first step:

- it still adds an extra node layer to every expression
- every AST walk pays an extra branch / variant alternative cost
- the parser starts manufacturing semantic scaffolding into what should remain a source-faithful syntax tree
- it increases memory footprint even if later “replacement” avoids a second allocation

#### Option B: parser creates ordinary expression nodes with an empty semantic slot

- no extra wrapper node
- no extra tree depth
- semantic pass later fills `implicit_cast = nullopt -> Some(...)`

This is the better interpretation of the original doc’s performance argument.

### Recommendation

Adjust the plan to prefer **Option B**:

- parser should initialize semantic slots to “empty / no cast”
- semantic pass should fill those slots
- structural semantic nodes should be reserved for semantics that cannot be encoded compactly

### B. Semantic data model in more detail

The first pass needs three levels of semantic data:

#### 1. Canonical type identity

This should answer:

- what semantic type an expression has
- whether two types are the same type for overload/signature purposes
- what cv/ref/pointer/array/function metadata belongs to that type

Recommended actual implementation shape:

- use repo-friendly compact storage (`InlineVector`) inside the type descriptor
- intern canonical types into a `TypeContext` and compare them by `CanonicalTypeId`
- prefer normalizing from existing `TypeSpecifierNode` data instead of inventing a heavyweight separate type graph in phase 1

Suggested real storage shape:

```cpp
enum class CanonicalTypeFlags : uint8_t {
	None = 0,
	IsPackExpansion = 1 << 0,
	IsFunctionType = 1 << 1,
};

struct CanonicalTypeDesc {
	Type base_type = Type::Invalid;
	TypeIndex type_index{};
	CVQualifier base_cv = CVQualifier::None;
	ReferenceQualifier ref_qualifier = ReferenceQualifier::None;
	InlineVector<PointerLevel, 4> pointer_levels;
	InlineVector<size_t, 4> array_dimensions;
	CanonicalTypeFlags flags = CanonicalTypeFlags::None;
	std::optional<FunctionSignature> function_signature;
};
```

This is not just a memory optimization; it also gives:

- O(1) canonical type equality via `type_id_a == type_id_b`
- cheaper overload/signature comparisons
- a stable compact type handle for semantic slots, cast tables, and diagnostics

#### 2. Expression semantic result

This is the value returned from expression rewriting and local semantic classification:

```cpp
enum class SemanticExprFlags : uint8_t {
	None = 0,
	IsDependent = 1 << 0,
	IsConstantEvaluated = 1 << 1,
	IsOverloadSet = 1 << 2,
};

struct SemanticExprInfo {
	CanonicalTypeId type_id{};
	ValueCategory value_category = ValueCategory::PRValue;
	SemanticExprFlags flags = SemanticExprFlags::None;
	CastInfoIndex cast_info_index{};
};

struct SemanticRewriteResult {
	ASTNode node;
	SemanticExprInfo info;
};
```

#### 3. Conversion plan

Current overload helpers mostly return rank. The semantic pass needs more than rank: it needs the **actual sequence of semantic operations** required.

Suggested design:

```cpp
enum class ConversionPlanFlags : uint8_t {
	None = 0,
	IsValid = 1 << 0,
	IsUserDefined = 1 << 1,
	BindsReferenceDirectly = 1 << 2,
	MaterializesTemporary = 1 << 3,
};

struct ConversionStep {
	StandardConversionKind kind;
	CanonicalTypeId target_type_id{};
};

struct ConversionPlan {
	ConversionRank rank = ConversionRank::NoMatch;
	ConversionPlanFlags flags = ConversionPlanFlags::None;
	InlineVector<ConversionStep, 3> steps;
	const FunctionDeclarationNode* conversion_function = nullptr;
	const ConstructorDeclarationNode* converting_constructor = nullptr;
};
```

Recommendation:

- use typed flag enums for compact sets of orthogonal booleans
- do not use bitmasks for mutually exclusive concepts such as `ValueCategory`, `ConversionContext`, or `StandardConversionKind`
- prefer explicit mask helpers over C++ bitfields if deterministic layout and easy debugging matter

### Recommendation

Extend `OverloadResolution` with a helper like `buildConversionPlan(from, to)` rather than letting `SemanticAnalysis` reverse-engineer a sequence from `can_convert_type(...)`.

That gives one canonical source for:

- conversion ranking
- ambiguity comparison
- rewrite-time cast insertion

### Constant evaluation as a semantic service

This should be explicit in the plan, not treated as optional decoration.

For C++20, semantic analysis must own or orchestrate evaluation in contexts that require constant expressions, including:

- array bounds
- enumerator initializers
- case labels
- `if constexpr`
- `noexcept(expr)`
- non-type template arguments
- `consteval` / required-immediate contexts as support grows

Codegen should not be the place that decides whether an expression is a valid constant expression. It should only consume:

- the already-resolved type information
- already-evaluated constant results where required

Practical recommendation:

- keep `ConstExpr::Evaluator` as the evaluator service
- make `SemanticAnalysis` the orchestrator that invokes it in required semantic contexts
- store compact handles/results in semantic metadata rather than forcing repeated ad-hoc re-evaluation in unrelated layers

### Constraints and concepts

The plan should explicitly reserve semantic ownership for:

- `requires`-expression checking
- constraint satisfaction after substitution
- constraint-aware overload viability and tie-breaking
- later subsumption rules as concepts support matures

This likely lands after the first conversion-focused phases, but it belongs to semantic analysis, not parser recovery and not backend lowering.

### Diagnostics ownership

The plan should state this explicitly: semantic analysis should become the primary owner of source-language diagnostics for:

- invalid implicit conversions
- overload no-match / ambiguity
- narrowing and invalid initialization forms
- invalid constant-expression-required contexts
- later constraint-satisfaction failures

Parser diagnostics should stay focused on syntax and parse-time recovery.

Codegen diagnostics should stay focused on lowering/backend invariants, not on discovering basic C++ type-law violations late.

### C. Semantic context stack

The pass should carry a small explicit context rather than relying on global state.

Suggested fields:

```cpp
enum class ConversionContext : uint8_t {
	None,
	FunctionArgument,
	Return,
	Initialization,
	Assignment,
	Condition,
	BinaryArithmetic,
	Comparison,
	Ternary,
	ReferenceBinding
};

enum class UserDefinedConversionPolicy : uint8_t {
	Disallow,
	Allow,
};

enum class SemanticContextFlags : uint8_t {
	None = 0,
	InConstantEvaluatedContext = 1 << 0,
	InsideTemplateDefinition = 1 << 1,
	InsideInstantiatedTemplate = 1 << 2,
};

struct SemanticContext {
	std::optional<CanonicalTypeId> expected_type_id;
	std::optional<CanonicalTypeId> current_function_return_type_id;
	ConversionContext conversion_context = ConversionContext::None;
	SemanticContextFlags flags = SemanticContextFlags::None;
	UserDefinedConversionPolicy user_defined_conversion_policy = UserDefinedConversionPolicy::Allow;
};
```

This keeps the pass deterministic and makes it much easier to reason about when:

- contextual `bool` applies
- user-defined conversion should be considered
- dependent expressions must be left untouched

It is also the right place to thread:

- constant-evaluation-required state
- constraints-satisfaction context
- whether diagnostics should be deferred or emitted immediately

Type-safety recommendation:

- `CanonicalTypeId`, `CastInfoIndex`, and similar handles should be non-arithmetic wrapper types
- use `enum class` for semantic policy switches and state machines
- avoid broad integer aliases that can silently mix unrelated IDs
- keep conversions explicit at API boundaries

### D. Detailed rewrite mechanics by node category

The pass should use a strict rule for every node kind:

#### Pure leaf nodes

Examples:

- `IdentifierNode`
- `NumericLiteralNode`
- `BoolLiteralNode`
- `StringLiteralNode`

Rule:

- usually return original `ASTNode`
- compute semantic info only
- allocate nothing unless dependent/template substitution forces a replacement

#### Unary / binary / ternary expression nodes

Rule:

1. rewrite children
2. compute builtin/operator-overload semantic path
3. if children unchanged structurally and only annotation updates are needed, keep the original node and fill semantic slots
4. otherwise allocate a replacement expression node
5. preserve any semantic overload metadata already attached to the old node

Note:

`BinaryOperatorNode` already has `copy_semantic_operator_resolution_from(...)`, which should be reused when cloning rewritten nodes.

#### Call nodes

Rule:

1. keep overload selection logic authoritative
2. after a callee is selected, rewrite each argument under parameter expectations
3. annotate each converted argument explicitly
4. rebuild the call only if at least one argument changed structurally

#### Statement nodes

Rule:

- rewrite child expressions/statements recursively
- because many statement classes have getters but no setters, rebuild the statement node when any child changes

This applies especially to:

- `ReturnStatementNode`
- `IfStatementNode`
- `ForStatementNode`
- `WhileStatementNode`
- `DoWhileStatementNode`

#### Block / function / namespace containers

Rule:

- rewrite children in order
- preserve original order and AST identity for unchanged children
- allocate a new container only when at least one child differs

### E. Root traversal strategy

A practical first traversal order:

1. top-level declarations
2. function definitions
3. statement trees inside functions
4. expressions inside statements

Do **not** attempt a generic “rewrite any `ASTNode` of any kind everywhere” entry point first. The current AST is heterogeneous enough that a declaration/statement/expression split will be easier to keep correct.

### F. Interaction with templates and dependent code

This is one of the most important scope boundaries.

#### Recommended rule

Do semantic normalization only when the subtree is semantically concrete enough.

That means:

- normalize ordinary non-template code after parse
- normalize instantiated template bodies after substitution/instantiation
- do not force full semantic normalization of dependent template bodies at definition time

#### Why

The parser currently performs template instantiation and delayed body handling during parsing. A pass that insists on fully resolving dependent expressions too early would either:

- duplicate parser/template logic, or
- reject code that should stay dependent until instantiation

#### Practical implementation shape

Use two entry points:

```cpp
std::vector<ASTNode> runTranslationUnitSemanticAnalysis(...);
ASTNode runInstantiatedBodySemanticAnalysis(ASTNode body, ...);
```

The second entry point is especially relevant for:

- generic lambda instantiated bodies
- template member function bodies
- deferred default arguments after template substitution

### G. Interaction with `auto` and generic lambdas

The plan should distinguish three cases:

#### 1. Ordinary `auto` variable declarations

These are the easiest migration target.

Recommended migration:

- parser may still recognize `Type::Auto`
- semantic pass finalizes the concrete `TypeSpecifierNode`
- declaration rewriting replaces the `type_node` only when deduction succeeded

#### 2. Generic lambda parameters

These are harder because the concrete types are tied to call-site deduction and instantiated bodies.

Recommended migration:

- keep parser and `LambdaInfo` deduction ownership initially
- add an instantiation-time semantic normalization hook that rewrites the instantiated body using the deduced parameter `TypeSpecifierNode`s
- only then remove codegen synthetic declaration fallbacks

#### 3. `auto` return types / `decltype(auto)`

These should be explicitly deferred until after the pass seam is working and ordinary `auto` variables are stabilized.

### H. What to move out of codegen first vs later

The plan should be more explicit here.

#### Move first

- standard numeric promotions/conversions in builtin binary expressions
- standard conversions in call arguments after overload selection
- standard return conversions
- contextual conversion to `bool` in control-flow statements

These are high-value and structurally straightforward, and they fit the annotation/slot model well.

#### Move later

- user-defined conversion materialization
- converting constructors in all initialization forms
- reference binding edge cases
- temporary lifetime extension
- full canonical signature comparison
- full constraint-aware overload viability/subsumption
- richer `noexcept` and exception-spec semantics
- constant-expression-required contexts beyond the initial must-have set

These have more interaction with overload resolution, lifetimes, and template instantiation state.

### Recommendation

Phase 2 should preferably handle **standard conversions first**, while still allowing existing user-defined conversion lowering to remain in place until the standard-conversion path is proven.

### I. File-by-file implementation sequence

This is the most concrete change order that fits the current codebase:

#### Step 1: infrastructure ✅ COMPLETED (PR #917)

- `src/SemanticTypes.h` (new)
- `src/SemanticAnalysis.h` (new)
- `src/SemanticAnalysis.cpp` (new)
- `src/FlashCppMain.cpp` (modified — pass seam, timing, stats)
- `src/CompilerIncludes.h` (modified — include for modular and unity builds)
- `tests/test_semantic_pass_ret0.cpp` (new — end-to-end test)

Work:

- add pass construction and timing
- keep pass no-op
- keep `main.cpp` consuming parser-owned roots for the first annotation-only slice

#### Step 2: AST node support

- `src/AstNodeTypes_Expr.h`
- `src/AstNodeTypes.cpp`
- `src/AstToIr.h`
- `src/IrGenerator_Expressions.cpp` or the relevant expression-dispatch file
- `src/IrGenerator_Expr_Conversions.cpp`

Work:

- add semantic-slot data structures
- teach debug printing
- teach expression dispatch and lowering from annotations

#### Step 3: conversion-plan support (done — Phases 11 + 19)

- `src/OverloadResolution.h`

Work:

- ✅ add `ConversionPlan` struct and `buildConversionPlan()` (Phase 11)
- add `TypeContext` / type interning support (done in Phase 1)
- ✅ reuse existing rank rules (Phase 11 delegates `can_convert_type` to `buildConversionPlan`)
- ✅ keep behavior matching existing overload ranking first (Phase 11 — all tests pass)
- ✅ extend `buildConversionPlan` to `TypeSpecifierNode`-level conversions (Phase 19)

#### Step 3b: semantic constant-evaluation integration

- `src/ConstExprEvaluator*.cpp`
- `src/SemanticAnalysis.cpp`

Work:

- make semantic analysis invoke constexpr evaluation in required contexts
- centralize “must be a constant expression here” checks in semantic analysis
- stop spreading those checks ad hoc across parser/codegen paths

#### Step 4: migrate function-call arguments

- `src/SemanticAnalysis.cpp`
- `src/IrGenerator_Call_Direct.cpp`

Work:

- semantic pass annotates argument nodes with conversion metadata
- codegen stops injecting standard conversions at this callsite

#### Step 5: migrate return conversions

- `src/SemanticAnalysis.cpp`
- `src/IrGenerator_Visitors_Namespace.cpp`

#### Step 6: migrate builtin binary arithmetic/comparison conversions

- `src/SemanticAnalysis.cpp`
- `src/IrGenerator_Expr_Operators.cpp`

#### Step 7: migrate control-flow conditions

- `src/SemanticAnalysis.cpp`
- statement node rebuild helpers as needed

#### Step 8: initializers and references

- `src/SemanticAnalysis.cpp`
- declaration/codegen sites currently doing local binding or conversion policy

#### Step 9: canonical signature cleanup

- `src/AstNodeTypes_DeclNodes.h`
- `src/SymbolTable.h`
- `src/OverloadResolution.h`

#### Step 9b: diagnostics ownership cleanup

- `src/SemanticAnalysis.cpp`
- current type-mismatch / overload-error sites in parser/codegen

Work:

- make semantic analysis the primary owner of:
	- type-conversion diagnostics
	- overload viability / ambiguity diagnostics
	- narrowing / invalid-initialization diagnostics
	- constant-expression-required diagnostics

Goal:

- reduce late codegen errors for semantic/type-law problems
- keep source locations and semantic context attached to the right diagnostic layer

#### Step 10: `auto` / generic lambda cleanup

- `src/Parser_Statements.cpp`
- `src/IrGenerator_Expr_Primitives.cpp`
- `src/IrGenerator_Lambdas.cpp`
- template/generic-lambda instantiation paths

### J. Additional helper APIs likely worth adding

To make the pass practical without brittle code, the plan should budget a few utility helpers:

#### `ASTNode` identity helper

Something like:

```cpp
struct ASTNodeIdentity {
	const void* pointer = nullptr;
	std::type_index type = typeid(void);
};
```

Purpose:

- memoization inside a single semantic pass
- node-identity comparisons for “did anything change?”
- optional side-table caches later

This may require adding a small helper on `ASTNode` instead of repeatedly poking at `std::any`.

#### Statement / expression rebuild helpers

Examples:

- `cloneExpressionWithNewChildren(...)`
- `cloneStatementWithNewChildren(...)`
- `attachImplicitCast(ASTNode expr, const SemanticExprInfo& from, CanonicalTypeId to, StandardConversionKind kind)`

These are not just convenience helpers; they reduce copy-paste bugs and keep semantic rewriting consistent. In the annotation-first design, “attach” should be the common path and “wrap” should be rare.

#### Packed slot / side-storage helpers

Examples:

- `CanonicalTypeId internCanonicalType(const TypeSpecifierNode&)`
- `uint16_t allocateImplicitCastInfo(CanonicalTypeId from, CanonicalTypeId to, StandardConversionKind kind, ValueCategory after)`
- `SemanticSlot& getSemanticSlot(ASTNode node)`

These helpers are important because they enforce the memory-layout rule: AST nodes keep tiny handles, while rich semantic payloads live in interning tables or chunked side storage.

### K. Performance guardrails and concrete budgets

The plan should define success conditions, not just “measure it.”

Recommended guardrails:

- semantic-pass time should be reported separately in perf logs
- unchanged translation units should not show large AST-node growth
- structural-node count should stay small relative to conversion-site count
- no phase should require a second full AST walk just to lower already-annotated casts

Recommended first measurements:

- total root count
- rewritten root count
- total new AST node allocations performed by `SemanticAnalysis`
- count of expressions carrying non-empty semantic slots
- count of structurally-created semantic nodes (should be near zero in the first slice)
- size of major expression node types before/after slot addition
- type-interner hit rate / unique canonical-type count
- pass time as a percentage of total frontend time

### L. Major risks and mitigations

#### Risk 1: semantic pass duplicates parser type logic

Mitigation:

- canonicalize from existing parser-produced `TypeSpecifierNode`
- do not invent a second type-construction system in phase 1

#### Risk 2: dependent template code gets normalized too early

Mitigation:

- allow dependent expressions/types to pass through unchanged
- run a second semantic entry point on instantiated bodies only

#### Risk 3: overload ranking and cast insertion diverge

Mitigation:

- share a `ConversionPlan` source between `OverloadResolution` and `SemanticAnalysis`

#### Risk 3b: semantic slots bloat the AST

Mitigation:

- keep slot representation handle-based and tightly packed
- intern canonical types and store `CanonicalTypeId`, not full type descriptors
- fall back to side tables if per-node slot growth is too costly

#### Risk 3c: constant evaluation stays fragmented

Mitigation:

- make semantic analysis the orchestrator for required constant-evaluation contexts early
- use one evaluator service rather than duplicating constant-folding decisions across layers

#### Risk 3d: weakly typed semantic handles leak implicit conversions across the codebase

Mitigation:

- use explicit wrapper types for semantic IDs and indexes
- use typed flag enums or policy enums instead of raw booleans where meaning matters
- keep arithmetic on handles impossible unless a helper intentionally exposes it

#### Risk 4: excessive AST churn hurts compile time

Mitigation:

- structural sharing by default
- append-only arena allocation
- per-node “changed?” checks before cloning

#### Risk 5: codegen and semantic pass both apply conversions during migration

Mitigation:

- phase each migrated context fully
- once a context is rewritten semantically, remove its codegen-local standard-conversion policy instead of leaving duplicate fallback paths
- **Phase 15 status:** All 11 codegen fallback sites for standard conversions now use hard enforcement. For standard arithmetic types in sema-normalized function bodies, codegen throws `InternalError` if sema missed the annotation — the fallback is no longer silent. Non-arithmetic types (struct, enum, user_defined, etc.) keep unconditional fallbacks since sema doesn't own those yet. Functions not normalized by sema (e.g. template instantiation member functions) are excluded via `sema_normalized_current_function_`. The dual-path "sema annotation wins; codegen policy is fallback" pattern is fully eliminated for standard arithmetic conversions.

#### Risk 6: diagnostics remain split across parser / sema / codegen

Mitigation:

- explicitly migrate type-law and overload-law diagnostics into semantic analysis
- keep backend errors focused on lowering/backend invariants rather than source-language semantic validity

### M. Explicit anti-goals

These should be written down so the project does not drift into a larger rewrite accidentally.

Do **not** make the first semantic pass:

- a full replacement for parser-time type formation
- a full replacement for template instantiation
- a global semantic graph builder for every declaration in the program
- a second monolithic AST with duplicated ownership of every node
- a broad “add setters everywhere” AST mutability project

### N. Document shape recommendation

The document is long, but the problem is also genuinely cross-cutting. So the right move is not to throw detail away; it is to separate:

- a short decision-oriented summary near the top
- the detailed design and migration notes below as a technical appendix

Recommendation:

- keep this detailed plan as the engineering reference
- trim repetition where the same idea is restated
- if needed later, add a short companion overview doc rather than deleting the useful detail here

### O. Final recommendation

The strongest long-term design remains: **add a semantic layer**.

But the strongest *practical* plan for FlashCpp is:

1. create a real post-parse semantic seam
2. normalize only the contexts where the standard most clearly requires semantic context
3. use empty semantic slots / annotations as the default representation, not universal wrapper nodes
4. unify conversion planning with overload ranking
5. only after that, expand into canonical type identity and `auto` / generic-lambda cleanup

That gives the project a better compiler architecture without sacrificing the “high speed compiler” goal that motivated your concern.
