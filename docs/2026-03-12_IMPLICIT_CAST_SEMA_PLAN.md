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
- generic lambdas currently rely on `LambdaInfo` deduced types and codegen-local fixes
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
- codegen re-reads deduced types in multiple places
- codegen also synthesizes replacement parameter declarations so lambda bodies do not see unresolved `Type::Auto`
- there is still a backend fallback from unresolved `Type::Auto` + size 0 to `int`/32-bit

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
- codegen still has a fallback path in `IrGenerator_Visitors_Namespace.cpp` that deduces from return expressions if `current_function_return_type_` is still `Type::Auto`

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

- parser currently uses `Type::Auto` as a placeholder for `decltype(auto)` as well
- this collapses two semantically different forms into one temporary representation

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

### Phase 1: establish the seam ✅ COMPLETED (PR #917)

Goal:

- introduce the semantic pass without changing behavior broadly

Work:

- add `SemanticAnalysis` files
- add timing hook in `main.cpp`
- add semantic-slot data structures and debug-print support
- add lowering support in `AstToIr` for semantic annotations
- keep the pass effectively no-op at first except for tracing / validation hooks

Exit criteria:

- clean build
- pass is measurable
- no test regressions

#### Implementation notes (PR #917)

New files added:

- `src/SemanticTypes.h` — all vocabulary types: `SemanticSlot` (8 bytes, `static_assert`'d), `CanonicalTypeId`, `CastInfoIndex`, `StandardConversionKind`, `ImplicitCastInfo`, `CanonicalTypeDesc`, `TypeContext`, `SemanticContext`, `ConversionPlanFlags`, `ConversionStep`, `SemanticPassStats`
- `src/SemanticAnalysis.h` / `src/SemanticAnalysis.cpp` — the traversal pass
- `tests/test_semantic_pass_ret0.cpp` — end-to-end test covering structs, namespaces, recursion, all control-flow statement types, ternary, array subscript, member function calls, and fold expressions

Pipeline integration in `src/FlashCppMain.cpp`:

- pass runs between `gLazyMemberResolver.clearCache()` and `AstToIr` construction
- timed via `PhaseTimer`, stats emitted under `--perf-stats`
- APIs take `const ASTNode&` to avoid unnecessary `std::any` copies

What Phase 1 does:

- walks all top-level nodes: functions, structs (member function bodies), namespaces, constructors, destructors, top-level variables
- recurses into all statement types: block, return, variable decl, if, for, while, do-while, switch, range-for, try-catch
- recurses into all non-leaf `ExpressionNode` variants via `std::visit`: binary/unary/ternary operators, function/constructor/member calls, member access, pointer-to-member access, subscripts, sizeof/alignof/typeid, new/delete, all four cast nodes, lambda (captures, parameters, body), fold/pack expressions, noexcept, initializer-list construction, throw expressions
- interns canonical types for function return types via `TypeContext`
- collects traversal statistics (roots, expressions, statements, canonical types)

What Phase 1 does NOT do:

- no actual semantic normalization or implicit cast insertion
- no semantic slot filling on expression nodes
- no `AstToIr` lowering changes for semantic annotations (deferred to Phase 2)
- `SemanticAnalysis` object is scoped to a block and destroyed before IR conversion — its `TypeContext` and cast-info table do not survive into later phases yet

Known issues resolved in Phase 2:

- `SemanticAnalysis` results now outlive the timing scope — the object is kept alive past `AstToIr` construction and `converter.setSemanticData(&sema)` wires up the connection
- Semantic annotations (`slots_filled`, `cast_infos_allocated`) now tracked and visible under `--perf-stats`

Known issues still to address:

- `TypeContext::intern()` uses linear scan — acceptable at current type counts, but needs hash map when all expressions are canonicalized (Phase 4 TODO)
- `CanonicalTypeDesc::operator==` for `FunctionSignature` does not compare all fields — acceptable for now
- `CastInfoIndex` uses `uint16_t` (max 65535 entries) — needs overflow guard when cast-info table grows large
- `normalizeStructDeclaration` only visits member function bodies, not member variable initializers or nested types
- `normalizeStatement` does not walk `ThrowStatementNode` or `LabelStatementNode` children
- `inferExpressionType` does not handle `BinaryOperatorNode` or `FunctionCallNode` — complex expressions return unknown type and fall back to codegen policy

### Phase 2: migrate the highest-value conversion contexts ✅ COMPLETED (return, call args, binary ops)

Goal:

- move the most error-prone standard conversions out of codegen-local logic

Work:

- ✅ return statements (standard primitive conversions)
- ✅ function-call arguments (simple non-overloaded functions)
- ✅ builtin binary arithmetic / comparison operands
- establish semantic-pass diagnostics ownership for these migrated contexts so codegen no longer reports them late

Exit criteria:

- delete corresponding ad-hoc conversion policy from:
	- `IrGenerator_Call_Direct.cpp` — dual-path added; original policy preserved as fallback for overloaded/variadic/struct cases
	- `IrGenerator_Expr_Operators.cpp` — dual-path added; original policy preserved as fallback for struct/user-defined operands
	- `IrGenerator_Visitors_Namespace.cpp` — dual-path added; original policy preserved as fallback for struct/user-defined conversions
- add regression tests for each migrated context ✅

#### Implementation notes (Phase 2 — all three contexts)

**Pre-requisites addressed:**

1. **`SemanticAnalysis` lifetime** — moved outside the block scope in `FlashCppMain.cpp` so the object (and its `TypeContext` + `cast_info_table_`) survives into `AstToIr` conversion. `converter.setSemanticData(&sema)` wires up the connection.

2. **SemanticSlot side table** — added `semantic_slots_: unordered_map<const void*, SemanticSlot>` to `SemanticAnalysis` (keyed by raw `ExpressionNode*` pointer from stable `gChunkedAnyStorage`). Added `getSlot(const void*)`, `setSlot(...)`, `allocateCastInfo(...)` helpers.

3. **AstToIr reads annotations** — added `sema_: const SemanticAnalysis*` member and `setSemanticData()` to `AstToIr`. All three IR lowering contexts check `sema_->getSlot(key)` before the local `generateTypeConversion` fallback.

**Core annotation infrastructure in SemanticAnalysis:**

- `inferExpressionType(node)` — infers `CanonicalTypeId` for `NumericLiteralNode`, `BoolLiteralNode`, and `IdentifierNode` (via a scope stack of `StringHandle → CanonicalTypeId` maps). Returns invalid when inference is not possible.
- `tryAnnotateConversion(expr_node, target_type_id)` — shared helper: if `inferExpressionType` succeeds and a standard (non-struct, non-enum, non-auto, non-pointer) conversion is needed, allocates an `ImplicitCastInfo` and fills the expression's slot. Returns `true` when annotation was written.
- `tryAnnotateReturnConversion(expr_node, ctx)` — delegates to `tryAnnotateConversion` with `ctx.current_function_return_type_id` as target.
- `tryAnnotateBinaryOperandConversions(bin_op)` — infers LHS/RHS types, computes common type with `get_common_type`, and calls `tryAnnotateConversion` on each operand that needs conversion.
- `tryAnnotateCallArgConversions(call_node)` — looks up the function by name in `symbols_`; if a single non-variadic match is found, annotates each argument with its parameter type.
- `determineConversionKind(from, to)` — maps `(Type, Type)` to `StandardConversionKind`; uses `get_integer_rank()` for C++20-correct promotion vs conversion classification.
- Scope stack — `normalizeBlock` now calls `pushScope()`/`popScope()`; `VariableDeclarationNode` registers the local; `normalizeFunctionDeclaration` registers parameters.
- Stats: `slots_filled` and `cast_infos_allocated` now tracked and reported under `--perf-stats`.

**Fixes applied after review (Devin + Gemini):**

- `Type::Auto` guard added to `tryAnnotateConversion` — prevents bogus 0-bit truncation for `auto` return functions
- `determineConversionKind`: int→long is now `IntegralConversion` (not `IntegralPromotion`); uses `get_integer_rank()` per C++20 [conv.prom]
- Parameter and local variable type registration uses `canonicalizeType()` instead of inline struct construction — handles pointer/array types correctly
- Unary +/- type inference applies integral promotion (result of `+short` is `int`)

**New tests:**

- `tests/test_ret_implicit_cast_int_to_long_ret0.cpp` — int→long return from literal, local variable, and function parameter
- `tests/test_ret_implicit_cast_float_to_int_ret0.cpp` — float→int and double→int return from local variable
- `tests/test_ret_implicit_cast_unsigned_ret0.cpp` — int→unsigned, int→long long, int→unsigned long return conversions
- `tests/test_call_implicit_cast_arg_ret0.cpp` — int→long and int→double function argument conversions
- `tests/test_binop_implicit_cast_ret0.cpp` — int+long→long (LHS promoted), int*unsigned→unsigned (LHS converted)

Test results: 1476 pass / 0 fail / 35 expected-fail (5 new tests added)

**Remaining known limitations (carried forward to Phase 4+):**

- Function call argument annotation handles only the simple single-overload lookup case; overloaded functions still use the codegen fallback
- `inferExpressionType` coverage for nested `BinaryOperatorNode` sub-expressions is complete; `FunctionCallNode` result type is also inferred

### Phase 3: initializer and reference-binding coverage ✅ COMPLETED (declaration initializers, assignment RHS)

Goal:

- cover the remaining correctness-critical conversion contexts

Work:

- ✅ declaration initializers (`long x = some_int;`, `double d = int_var;`)
- ✅ assignment RHS (`long x; x = some_int;`)
- ✅ `inferExpressionType` extended to cover `BinaryOperatorNode` and `FunctionCallNode` results
- ✅ Bug fix: `int i = double_var;` was storing raw IEEE-754 bits instead of truncating
- member initialization — deferred to Phase 4+
- constructor-argument normalization — deferred to Phase 4+
- reference binding — deferred to Phase 4+
- temporary materialization — deferred to Phase 4+
- conditional-expression conversions — deferred to Phase 4+
- contextual `bool` — deferred to Phase 4+
- integrate semantic-orchestrated constant evaluation — deferred to Phase 4+

Exit criteria:

- no remaining policy-style `generateTypeConversion(...)` calls for these contexts — partially met; declaration initializers and assignment now go through sema path; remaining contexts still use codegen fallback

#### Implementation notes (Phase 3)

- `normalizeStatement` for `VariableDeclarationNode` calls `tryAnnotateConversion(*init, decl_type_id)` before visiting the initializer expression.
- `normalizeExpression` for `BinaryOperatorNode` with `op == "="` infers the LHS type and calls `tryAnnotateConversion` on the RHS.
- `IrGenerator_Stmt_Decl.cpp` dual-path: sema annotation wins when present; `can_convert_type` fallback handles any case the sema pass could not infer (e.g., function-call return values that are still unresolved at annotation time).
- `inferExpressionType` extended: `BinaryOperatorNode` arithmetic→`get_common_type`, comparisons/logical→`bool`, assignment→LHS type, comma→RHS type; `FunctionCallNode`→return type from `decl_node().type_node()`.
- Test suite hygiene: six tests that relied on implicit shell exit-code truncation (e.g., returning 330 and depending on 330 % 256 = 74) now have an explicit `% 256` in their return expressions.

**New tests:**

- `tests/test_decl_init_implicit_cast_ret0.cpp` — double→int, float→int, int→double, int→float initializer conversions

Test results: 1477 pass / 0 fail / 35 expected-fail (1 new test added)

**Remaining known limitations (carried forward to Phase 4+):**

- Function call argument annotation handles only the simple single-overload lookup case; overloaded functions still use the codegen fallback
- Reference-binding initializer conversions are not yet annotated
- Member initialization and constructor-argument conversions are not yet annotated

### Phase 4: canonical type identity cleanup ✅ COMPLETED

Goal:

- stop using syntax-node heuristics where semantic canonical type identity is required

Work:

- ✅ `TypeContext::intern()` — replaced O(n) linear scan with O(1) `std::unordered_map<CanonicalTypeDesc, CanonicalTypeId>` (hash specialisation in `SemanticTypes.h`)
- ✅ `inferExpressionType` for cast expressions — added `StaticCastNode`, `ConstCastNode`, `ReinterpretCastNode` cases that return the cast target type; unblocks annotation of expressions like `long y = (int)x + z`
- ✅ `canonical_types_match(CanonicalTypeId, CanonicalTypeId)` helper — added to `OverloadResolution.h`; expresses intent of canonical equality without hardcoding the `==` operator; used in `tryAnnotateCallArgConversions` to skip annotation when argument and parameter types already match
- ✅ `tryAnnotateCallArgConversions` — reconciled with overloaded functions: now uses `symbols_.lookup_all()` and matches the correct overload by `DeclarationNode` pointer identity (the parser already resolved the overload; we recover the `FunctionDeclarationNode` that wraps it); falls back to count-based selection when pointer match fails

Exit criteria:

- signature equivalence and overload ranking depend on canonical type IDs, not size/type fallbacks ✅

#### Implementation notes (Phase 4)

**Task 1 — TypeContext hash map:**

- Added `std::hash<CanonicalTypeDesc>` specialisation in `SemanticTypes.h` using a polynomial combiner over all fields (base type, type index, cv, ref qualifier, pointer levels, array dimensions, flags, optional function signature).
- Added `std::unordered_map<CanonicalTypeDesc, CanonicalTypeId> index_` private member to `TypeContext`.
- `TypeContext::intern()` now does `index_.find(desc)` first; only appends to `types_` on a cache miss and inserts into `index_`.

**Task 2 — inferExpressionType for cast expressions:**

- In `inferExpressionType()`, added a single `if constexpr` branch that handles `StaticCastNode`, `ConstCastNode`, and `ReinterpretCastNode` by reading `target_type()` and calling `canonicalizeType()` on it.
- `DynamicCastNode` is intentionally excluded because its target type always involves a pointer/reference and `tryAnnotateConversion` already guards against that.

**Task 3 — canonical_types_match:**

- Added `inline bool canonical_types_match(CanonicalTypeId a, CanonicalTypeId b)` to `OverloadResolution.h` (includes `SemanticTypes.h`).
- Used in `tryAnnotateCallArgConversions` to short-circuit annotation when `arg_type_id == param_type_id`.

**Task 4 — SymbolTable::lookup_function unification:**

- Replaced `symbols_.lookup(name)` (returns first overload) with `symbols_.lookup_all(name)` (returns all overloads).
- Primary match: find the `FunctionDeclarationNode` whose `decl_node()` address equals `&call_node.function_declaration()` — the parser stored a reference to the same stable object.
- Fallback 1: if pointer match fails and only one overload exists, use it.
- Fallback 2: if multiple overloads and no pointer match (template instantiation, indirect call), pick the first one whose parameter count equals the argument count.
- Fallback 3: if no viable fallback, return without annotating (codegen handles it).

**Known limitation discovered during Phase 4:**

Calling an overloaded function where one overload takes `int` and another takes `long` results in the `int` overload always being selected at runtime when the argument value is `0L`. This is a pre-existing codegen bug in FlashCpp (verified to exist in baseline before Phase 4 changes). Documented in `docs/KNOWN_ISSUES.md`.

**New tests:**

- `tests/test_cast_expr_type_inference_ret0.cpp` — `(int)f` and `static_cast<long>(x)` as sub-expressions; result type correctly inferred and annotations applied
- `tests/test_overload_call_annotation_ret0.cpp` — pointer-identity matching in `tryAnnotateCallArgConversions`; int→long argument annotation through non-overloaded functions

Test results: 1486 pass / 0 fail / 36 expected-fail (2 new tests added)

**Remaining known limitations (carried forward to Phase 5+):**

- Overload ranking for functions where argument types differ in size (pre-existing codegen limitation)
- `DynamicCastNode` target type not inferred (always pointer/reference; not needed for scalar annotation)
- `SymbolTable::lookup_function` still uses syntax-node comparison; full migration to canonical type IDs deferred to Phase 5

### Phase 5: `auto` / generic lambda cleanup

Goal:

- remove transitional backend fallbacks and synthetic declaration hacks

Work:

- keep ordinary parser-side `auto` deduction where the parser still depends on it, but add semantic-pass validation/canonicalization around it
- move generic lambda parameter normalization out of `IrGenerator_Lambdas.cpp` into an instantiation-time semantic hook
- remove synthetic lambda parameter declarations once that hook exists
- remove `Type::Auto` runtime fallbacks in codegen
- remove codegen-side fallback deduction of ordinary function `auto` return type
- add distinct handling for `decltype(auto)` so it no longer piggybacks ambiguously on plain `Type::Auto`
- extend the semantic layer to own constraint / requires diagnostics on instantiated generic code as support grows

Exit criteria:

- generic lambda bodies and signatures no longer depend on codegen-local synthetic declarations
- `Type::Auto` no longer reaches backend arithmetic/lvalue lowering on supported paths
- ordinary supported functions no longer depend on codegen to finalize `auto` return type
- `decltype(auto)` has an explicit semantic-resolution path instead of reusing plain `auto` heuristics

#### Phase 5 implementation notes (2026-03-15)

**Task 4 — `decltype(auto)` separation (complete)**

- Added `Type::DeclTypeAuto` enum value after `Type::Auto` in `AstNodeTypes_TypeSystem.h`.
- `Parser_TypeSpecifiers.cpp` now emits `Type::DeclTypeAuto` for `decltype(auto)` instead of reusing `Type::Auto`.
- All switch/if statements were updated to handle `Type::DeclTypeAuto` alongside `Type::Auto` in the following files:
  - `AstNodeTypes.cpp`
  - `AstNodeTypes_TypeSystem.h`
  - `NameMangling.h`
  - `IrType.cpp`
  - `Parser_Expr_QualLookup.cpp`
  - `SemanticAnalysis.cpp`
  - `LambdaHelpers.h`
  - `IrGenerator_Lambdas.cpp`
  - `IrGenerator_Expr_Primitives.cpp`
  - `IrGenerator_Call_Direct.cpp`
  - `IrGenerator_Expr_Operators.cpp`
  - `IrGenerator_Stmt_Decl.cpp`
  - `ExpressionSubstitutor.cpp`
  - `Parser_Templates_Inst_Deduction.cpp`
  - `Parser_Expr_PrimaryExpr.cpp`
  - `Parser_Expr_ControlFlowStmt.cpp`
  - `Parser_Decl_FunctionOrVar.cpp`
  - `Parser_FunctionBodies.cpp`
- `deduce_and_update_auto_return_type` now handles both `Type::Auto` and `Type::DeclTypeAuto`.
- Native type entry for `decltype(auto)` added to `gNativeTypes` in `AstNodeTypes.cpp`.
- Regression test: `tests/test_decltype_auto_forward_ret0.cpp`.

**Task 1 — auto return-type semantic-pass finalisation (complete infrastructure; codegen fallback retained as transition)**

- Added `SemanticAnalysis::resolveRemainingAutoReturns()` and `resolveAutoReturnNode()` in `SemanticAnalysis.cpp`.
- After the main normalization pass, a second sweep iterates over all top-level function and struct-member declarations. Any function whose return type is still `Type::Auto` or `Type::DeclTypeAuto` after parser-time deduction gets a second call to `parser_.deduce_and_update_auto_return_type()`. This resolves cases like inline friend functions where the enclosing struct was incomplete at parse time.
- `Parser::deduce_and_update_auto_return_type` was made accessible to `SemanticAnalysis` via a `friend class` declaration in `Parser.h`.
- The codegen fallback in `IrGenerator_Visitors_Namespace.cpp` is retained with a `TODO` comment. It now covers both `Type::Auto` and `Type::DeclTypeAuto` and will become `InternalError` once the semantic pass resolution is validated as complete for all supported cases.
- Regression test: `tests/test_phase5_auto_return_sema_ret0.cpp`.

**Task 2 — generic lambda parameter normalization hook (complete)**

- Added `resolved_param_nodes` (mutable `std::vector<ASTNode>`) to `LambdaInfo` in `IrGenerator.h`.
- Added public `SemanticAnalysis::normalizeGenericLambdaParams(const LambdaInfo&) const` which pre-builds resolved declaration nodes for auto/DeclTypeAuto parameters, using the deduced types already stored in `lambda_info.deduced_auto_types`.
- `IrGenerator_Lambdas.cpp::generateLambdaOperatorCallFunction` and `generateLambdaInvokeFunction` now call `sema_->normalizeGenericLambdaParams(lambda_info)` before the symbol-table registration loop, then use `resolved_param_nodes` when available. The inline creation of synthetic declarations (`makeSyntheticDeducedLambdaParamDecl`) is retained as a legacy fallback in case the semantic pass wasn't run.
- Regression test: `tests/test_phase5_generic_lambda_sema_ret0.cpp`.

**Task 3 — `Type::Auto` runtime guards (infrastructure updated; full promotion deferred)**

- `applyTransitionalAutoRuntimeFallback` in `IrGenerator_Expr_Primitives.cpp` updated to also handle `Type::DeclTypeAuto` and annotated with a `TODO` comment for the future `InternalError` promotion.
- All four `if (pointee_type == Type::Auto || pointee_size == 0)` guards updated to also check `Type::DeclTypeAuto`.
- The full promotion to `InternalError` is deferred until Task 2 is verified to prevent all `Type::Auto` from reaching identifier lowering on supported code paths.

**Known remaining limitations (Phase 5+):**

- `decltype(auto)` does not yet preserve reference/value-category semantics (reference forwarding); it deduces the same as plain `auto` for now. Full `decltype(auto)` reference-preservation requires the semantic pass to inspect the value category of the return expression.
- The codegen auto-return fallback is not yet an `InternalError`; it becomes one in a follow-up once all supported cases are covered by parser + sema second-pass.
- `applyTransitionalAutoRuntimeFallback` is not yet an `InternalError`; will follow when generic lambda param deduction is guaranteed complete before body codegen.



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

#### Step 3: conversion-plan support

- `src/OverloadResolution.h`

Work:

- add `ConversionPlan` helpers
- add `TypeContext` / type interning support
- reuse existing rank rules
- keep behavior matching existing overload ranking first

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
