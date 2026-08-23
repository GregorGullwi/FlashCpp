# 2026-08-16 Exhaustive Expression Rewrite and Sema Boundary Plan

## Status

Phase 0, Phase 1, Phase 2, and Phase 3 are complete as of 2026-08-16.
Phases 4 and 5 are in progress: the current branch contains the explicit
ownership/provenance boundary slice and the first structural-reuse cleanup,
but their full exit criteria are not yet met.
This document describes follow-up work after PR #1871, which added the missing
`NewExpressionNode` template-substitution path. The plan is intentionally
broader than that fix: its goal is to make the same class of omission difficult
to compile, easy to diagnose, and impossible to reach code generation silently.

The completed Phase 0-3 work centralizes structural expression traversal and
reconstruction, but it does not yet establish the lifecycle ownership contract
described in Phase 4. Parser-side pack-expansion orchestration and unresolved
pack nodes on deferred template-member surfaces remain supported until concrete
materialization.

### Completed Phase 0/1 implementation record

- Added `src/ExpressionStructure.h` as the canonical structural schema for all
  current `ExpressionNode` alternatives.
- Classified leaves explicitly and described named children with stable roles,
  including type children, receivers, optional array bounds, template
  arguments, call/constructor/placement arguments, lambda structure, folds,
  and pack patterns.
- Used exhaustive `std::visit` dispatch with a dependent `static_assert` for
  undeclared alternatives. The variant-size value is used only for legacy
  direct-node dispatch; it is not the exhaustiveness mechanism.
- Migrated expression recursion in `AstTraversal` and
  `PostParseBoundaryChecker` to the schema. Statement/declaration traversal
  and ownership/provenance decisions were left unchanged.
- Registered the new header in both Visual Studio project/filter pairs.
- Added `tests/test_expression_child_surfaces_ret42.cpp` and retained the
  existing new-expression, member-call, constructor, empty-pack, aggregate,
  and delegating-constructor regressions.

Validation for this stage: the MSVC build completed with 0 warnings/errors;
all focused regressions passed; the `<any>` and `<deque>` probes reached their
documented out-of-scope `_Seek_to`/`view_interface::operator==` stops; the full
suite passed 2,887 regular tests and 247 expected-failure tests; and
`git diff --check` passed. The known non-dependent-receiver pack-call lookup
gap is recorded in `docs/KNOWN_ISSUES.md`.

This stage intentionally did not add the general expression rewriter, remove
the `ExpressionSubstitutor` fallback, consolidate parser substitution
ownership, or introduce lifecycle ownership states. In particular,
`has_template_body_position()`-based boundary skips remain a Phase 4 finding,
not a Phase 1 behavior change.

### Completed Phase 2 implementation record

- Added `src/ExpressionRewriter.h` as an exhaustive structural reconstruction
  layer over the canonical `ExpressionStructure` schema.
- Added role-aware one-to-one child rewriting and zero-to-many flat-map
  rewriting for call, constructor, placement, and new-expression argument
  sequences.
- Reused `copyCallMetadataWithTransformedTemplateArguments` and preserved
  constructor resolution, call lookup/name/return-type metadata, source
  tokens, operator metadata, lambda metadata, and new-expression flags during
  structural rebuilding.
- Migrated `ExpressionSubstitutor` constructor/new/operator and remaining
  structural expression families onto the rewriter. Unknown expression
  alternatives fail through the exhaustive rewriter; non-expression statement
  and declaration children still intentionally pass through unchanged to their
  existing parser owners.
- Added reduced metadata and pack-sequence regressions in
  `test_expression_rewriter_metadata_ret42.cpp` and
  `test_expression_rewriter_pack_sequences_ret42.cpp`.

Validation for this stage: the MSVC build completed with 0 warnings/errors;
all focused regressions passed; the `<any>` and `<deque>` probes reached the
same documented out-of-scope `_Seek_to`/`view_interface::operator==` stops;
the full suite passed 2,889 regular tests and 247 expected-failure tests; and
the compiler-source audit found no standard-library or vendor-name behavior
special cases. The non-dependent receiver lookup gap remains documented and
unchanged. Parser-side expression dispatch consolidation was completed in
Phase 3; explicit lifecycle ownership remains Phase 4.

### Completed Phase 3 implementation record

- Added the complete `TemplateEnvironment` to `TemplateBodySubstitutionState`.
  Context-based substitution now passes its existing environment, while legacy
  parameter/argument entry points build the equivalent environment once.
- Routed wrapped and direct expression surfaces through one
  `ExpressionSubstitutor` entry point, preserving the current owner type and
  parser substitution scope. Structural expression reconstruction is now
  centralized, although parser-side pack-expansion orchestration remains for
  statement/initializer surfaces and contexts that need parser deduction data.
- Removed the parser's per-alternative expression routing and retained parser
  ownership only for declarations, types, statements, and scope-sensitive
  reconstruction.
- Moved `SizeofPackNode` and `FoldExpressionNode` materialization into the
  substitutor so pack counts, empty identities, non-type folds, and complex
  pack expressions continue to normalize before sema.
- Rebuilt parser pack-identifier replacement through the exhaustive
  `ExpressionRewriter`; type children remain structural leaves for this
  identifier-only operation.
- Kept rebuilt dependent alias specifiers and their registered source
  `TypeIndex` aligned. This preserves each syntactic array layer exactly once
  when canonical expression substitution resolves a current-instantiation
  member type, including `sizeof` over a dependent alias.

Phase 4 findings are substantially addressed: ownership is now explicit,
concrete materialized roots are checked independently of saved provenance,
late-root registration is guarded, and boundary diagnostics carry owner/child
paths and source tokens. The documented non-dependent receiver lookup gap is
unchanged.

### Phase 4 implementation-progress record (2026-08-23)

- Added `AstOwnershipPhase` to body-bearing function, constructor, destructor,
  and struct roots. Materialization transitions roots to
  `ConcreteMaterialized`; parser pattern/deferred-body setters preserve the
  parser-owned states; successful normalization marks roots
  `SemaNormalized`.
- Kept saved template body/initializer positions and template-pattern identity
  as provenance only. `PostParseBoundaryChecker` now skips only
  `ParserPattern` and `ParserDeferredBody`, so a concrete clone is checked even
  when it retains deferred-template provenance.
- Added sema normalized-root tracking and IR checks for normalized function,
  constructor, destructor, struct, and namespace entry points.
- Guarded late-materialized callable registration against parser-pattern/
  deferred roots, asserted top-level tracking synchronization, and made
  `extern` linkage-block extraction preserve late roots and their pending-sema
  registration. Shape-only/forward struct identities remain registerable for
  later upgrade while their parser-owned phase prevents sema consumption.
- Added role-indexed boundary paths such as
  `NewExpression.ConstructorArgument[0]`, alongside owner, node kind, token,
  line/column, and file index.
- Added the internal
  `SemanticAnalysis:ConcreteBodyRejectsParserOnlyHelperBeforeNormalization`
  regression in the doctest compiler harness. It parses an ordinary concrete
  function, injects a forbidden pack helper directly into the test-owned AST
  under `UnaryOperator.Operand[0]`, and proves rejection before semantic
  normalization or IR without adding test syntax to the production compiler.
- Constructor member/base/delegating initializer storage now transitions a
  parser-owned constructor to `ParserDeferredBody` whenever it retains a
  top-level pack helper, and returns to `ConcreteMaterialized` after
  materialization once substitution has removed the pack. The boundary
  checker no longer relies on a constructor-specific compatibility skip.
- Added `test_lifecycle_deferred_pack_surface_ret42.cpp` and
  `test_lifecycle_materialized_body_provenance_ret42.cpp` for the deferred and
  concrete-provenance cases.

The lifecycle-specific Phase 4 exit criterion is now met: the internal AST
regression proves rejection before sema/IR, and constructor initializer packs
carry an explicit parser-owned phase until concrete materialization. Codegen-synthetic
trivial constructors have an explicit documented treatment: their
`FunctionDeclOp` carries `IrFunctionLifecycle::CodegenSyntheticTrivialConstructor`,
and the object converter validates their synthetic declaration shape. They are
emitted from finalized `StructTypeInfo` after semantic analysis and
intentionally do not receive `AstOwnershipPhase`, because there is no
constructor AST root. Their lifecycle tag is compiler-internal and cannot
be exercised by a source-language test; existing template default-member-
initializer regressions cover the emitted behavior, while the IR lifecycle tag
and object-converter guard directly enforce the metadata invariant.

### Phase 5 implementation-progress record (2026-08-23)

- `TemplateExpressionEquivalence.cpp` now collects structural expression
  children through `ExpressionStructure::visitExpressionChildren` for equality
  and hashing. Structural roles are compared/hashed generically while
  operator flags, node flags, callee identity, type/path metadata, template
  argument roles, and lambda metadata remain intentional semantic checks; lambda
  bodies remain intentionally excluded.
- `exprContainsIdentifier` in
  `Parser_Templates_Substitution.cpp` now uses `AstTraversal`, reusing the
  canonical expression traversal for identifiers and template-parameter
  references instead of maintaining a separate recursive expression walk.
- Duplicate structural equality/hash range helpers were removed where the
  shared schema subsumes them.
- Lambda implicit-capture discovery in `Parser_Core.cpp` now keeps its local
  identifier, nested-lambda, and implicit-`this` policy while traversing every
  expression child through `ExpressionStructure`. The new
  `test_lambda_capture_new_constructor_arg_ret42.cpp` covers a previously
  omitted `new` constructor-argument surface.
- The two explicit-template-argument dependence walkers in
  `Parser_Templates_Params.cpp` now use the shared schema for their structural
  child checks while retaining their distinct qualified-name, placeholder-type,
  and fold policies.
- `RebindStaticMemberAst::tryRebindExpressionChildren` now reconstructs every
  non-call expression through the exhaustive `ExpressionRewriter`; calls keep
  their local static-member target-selection and metadata-copy policy.

The Phase 5 exit criterion is not yet met. Remaining expression-kind switches
in constexpr evaluation, overload form classification, boundary diagnostics,
and codegen are intentional semantic dispatches. Follow-up review remains for
the mixed structural/semantic deferred-dependence walk in
`Parser_Expr_PrimaryExpr.cpp`, static-member call rebinding, and the legacy
`substitute_template_params_in_expression` path in
`Parser_Templates_Inst_Substitution.cpp`; those paths should migrate only when
their lookup and environment policies can be preserved explicitly. Phase 4's
lifecycle-specific exit criterion is complete independently of this cleanup.

## Problem statement

FlashCpp currently describes expression structure in several independent
places:

- `ExpressionNode` lists the expression alternatives in
  `src/AstNodeTypes_Expr.h`.
- `AstTraversal` separately lists the children of many expression types in
  `src/AstTraversal.h`.
- `Parser::substituteTemplateParametersWithState` has its own expression-kind
  dispatch in `src/Parser_Templates_Substitution.cpp`.
- `ExpressionSubstitutor::substitute` has another dispatch in
  `src/ExpressionSubstitutor.cpp`.
- `PostParseBoundaryChecker::visitExpression` has another child traversal in
  `src/SemanticAnalysis.cpp`.
- Semantic analysis, constexpr evaluation, and IR conversion contain further
  expression-kind dispatches with different responsibilities.

Before PR #1871, `NewExpressionNode` was present in `ExpressionNode` and in the
read-only traversal, but was absent from the expression substitutor. The
substitutor's unknown-node fallback returned the original node unchanged.
Consequently a concrete function-template instantiation could retain a
`PackExpansionExprNode` in `new T(args...)`, and the first hard failure occurred
during IR conversion.

There is already a post-parse boundary checker for parser-only fold and pack
nodes. It runs for initial roots and roots obtained through
`takePendingSemanticRoots()`. However, its function handling currently skips a
function whenever `has_template_body_position()` is true, even if that function
has since acquired a concrete materialized body. This mixes provenance (the
function originated from a deferred template body) with lifecycle state (the
body is now concrete and sema-owned). That broad skip can hide exactly the late
materialization errors the boundary is meant to catch.

Semantic analysis also currently tolerates unresolved `PackExpansionExprNode`
instances on deferred template-member function surfaces. Phase 4 must
distinguish those surfaces from concrete materialized bodies rather than
rejecting every pack node indiscriminately.

The architectural problem is therefore not only one missing `if` branch. It is
the combination of:

1. duplicated descriptions of expression structure;
2. non-exhaustive transformation with a permissive fallback;
3. split substitution ownership between the parser and
   `ExpressionSubstitutor`;
4. lifecycle checks based on overlapping flags instead of an explicit AST
   ownership state.

## Goals

- Adding an `ExpressionNode` alternative must fail the compiler build until its
  rewrite and traversal behavior is explicitly declared.
- All structural expression rewriting must use one canonical child model.
- Template expression substitution must have one owner.
- A concrete materialized body must be checked before sema owns it, regardless
  of whether it originated as a deferred template body.
- Parser-only helper nodes must never reach a sema-normalized concrete body or
  IR. Deferred template-pattern/member-body surfaces may retain them until
  concrete materialization.
- Pack expansion must remain an arity-changing template operation, not a
  codegen fallback.
- Diagnostics must identify the owning function/type, child role, node kind,
  and source token when an ownership invariant fails.
- The migration must preserve current behavior and remain warning-clean under
  MSVC and clang.

## Non-goals

- This plan does not redesign overload resolution, constant evaluation, or IR
  lowering.
- It does not require every compiler pass to use an identical semantic visitor;
  passes may still dispatch by node kind when their operations differ.
- It does not move valid dependent template-pattern bodies into sema early.
- It does not permit sema or codegen to guess how an unresolved pack should be
  expanded.
- It does not add standard-library or vendor-name handling.

## Target invariants

The completed architecture should enforce these invariants:

1. **Expression schema invariant:** every `ExpressionNode` alternative is
   classified explicitly as a leaf or as a node with named children.
2. **Rewrite invariant:** every expression rewriter explicitly handles every
   alternative. There is no generic "return unchanged" fallback for an unknown
   expression kind.
3. **Pack invariant:** `FoldExpressionNode` and `PackExpansionExprNode` may
   remain on parser-owned template-pattern/deferred surfaces, but must be
   eliminated before a concrete materialized body is presented to sema. The
   current implementation still permits unresolved pack expansions on deferred
   template-member bodies; Phase 4 makes the distinction explicit.
4. **Lifecycle invariant:** provenance flags do not decide ownership. A
   materialized concrete body is checked and normalized even when it retains a
   saved template body position for diagnostics or caching.
5. **IR invariant:** every root accepted by IR has completed semantic
   normalization. IR may assert this state but may not repair it.

## Proposed architecture

### 1. Canonical expression child schema

Add a small expression-structure facility, preferably in a new header such as
`src/ExpressionStructure.h`. It should be the single source of truth for the
children of each `ExpressionNode` alternative.

The schema must distinguish child roles because not all children are rewritten
the same way:

- one expression child, such as the operand of a unary expression;
- an optional expression child, such as an array bound;
- a type child, such as the allocated type of `NewExpressionNode`;
- a fixed expression sequence, such as lambda captures;
- an arity-changing argument sequence, such as call, constructor, placement,
  or `new` constructor arguments;
- a statement/body child, such as a lambda body.

A useful interface shape is:

```cpp
enum class ExpressionChildRole {
	Operand,
	Condition,
	TrueBranch,
	FalseBranch,
	Receiver,
	TemplateArgument,
	CallArgument,
	ConstructedType,
	ArrayBound,
	ConstructorArgument,
	PlacementArgument,
	Body,
};

template <typename Visitor>
void visitExpressionChildren(const ExpressionNode& expression, Visitor&& visitor);
```

The exact enum can be adjusted, but child roles should be stable and useful in
diagnostics. Do not encode semantic behavior into the schema; it describes
structure and ownership only.

`NewExpressionNode` must expose all four relevant surfaces through this schema:

- `type_node()` as `ConstructedType`;
- `size_expr()` as `ArrayBound` when present;
- each `constructor_args()` element as `ConstructorArgument`;
- each `placement_args()` element as `PlacementArgument`.

#### Exhaustiveness mechanism

Implement the schema with `std::visit` and an overload for every variant
alternative. Leaf alternatives must use an explicit leaf overload or
`ExpressionNodeTraits<T>::is_leaf`; they must not fall into a generic catch-all.

If an `ExpressionNode` alternative is added, compilation should fail because
the visitor cannot be invoked for that type. If a templated visitor is used for
ergonomics, its final branch must contain a dependent `static_assert(false)`
rather than silently treating the type as a leaf.

Avoid relying only on `std::variant_size_v<ExpressionNode>` assertions. A count
assertion detects additions but does not document the new node's children and
can be "fixed" by changing a number.

#### Read-only traversal migration

Migrate the expression portion of `AstTraversal::visitASTImpl` to the canonical
schema. Statement/declaration traversal can remain in `AstTraversal.h` during
the first stage. This removes the first duplicate expression child list and
proves that the schema can cover current traversal behavior.

### 2. Exhaustive structural expression rewriter

Add a reusable reconstruction layer, for example
`src/ExpressionRewriter.h`. It should rebuild nodes after recursively rewriting
their children while preserving node-specific metadata.

The base operation should return one `ASTNode` for one expression:

```cpp
class ExpressionRewriter {
public:
	ASTNode rewrite(const ASTNode& expression);

protected:
	virtual ASTNode rewriteLeaf(const ASTNode& expression) = 0;
	virtual void rewriteArgument(
		const ASTNode& argument,
		ExpressionChildRole role,
		ChunkedVector<ASTNode>& output) = 0;
};
```

This is illustrative rather than a required virtual design. A CRTP or callable
policy is preferable if it keeps the hot path simpler. The important property
is separate support for:

- one-to-one child rewriting; and
- one-to-zero-or-many rewriting for argument sequences.

Pack expansion changes arity. Hiding all children behind a one-to-one
`transform(child) -> child` API would force special cases back into individual
node handlers. Argument sequences therefore need a `flatMap`/append operation.

#### Metadata preservation

Each rebuilt node must preserve the metadata currently copied by specialized
helpers, including:

- resolved/mangled call targets;
- receiver and qualified-name information;
- parser return-type hints;
- definition lookup records;
- explicit template arguments;
- value-category and cast-related parser metadata where stored on the AST;
- source tokens;
- `NewExpressionNode` array/value/brace initialization flags.

Before introducing generic reconstruction, inventory existing helpers such as
`copyCallMetadataWithTransformedTemplateArguments` and
`makeRebuiltConstructorCallNode`. Reuse them rather than creating parallel
metadata-copy code.

#### No permissive fallback

Remove the permissive behavior for an unrecognized *expression* from
`ExpressionSubstitutor::substitute`:

```cpp
// For any other node type, return as-is
return expr;
```

Explicit leaf nodes may be returned unchanged. An unrecognized expression node
must be a compile-time error in the visitor or an `InternalError` only at a
truly dynamic boundary that cannot be made exhaustive.

The existing return for declaration and statement children is not the target of
this change: those surfaces remain owned by the parser's declaration/statement
substitution passes. The invariant is that an unknown expression alternative
must not be silently returned unchanged.

### 3. Make `ExpressionSubstitutor` the single expression-substitution owner

Today `Parser::substituteTemplateParametersWithState` partially rewrites
expressions and selectively forwards some variants to `ExpressionSubstitutor`.
This creates two dispatch tables and makes correctness depend on remembering to
add routing statements in both places.

The target flow should be:

```text
Parser template instantiation
  -> build TemplateEnvironment / TemplateBodySubstitutionState
  -> ExpressionSubstitutor::substitute(expression)
       -> exhaustive structural rewrite
       -> type/value substitution hooks
       -> argument pack flat-map hook
  -> return concrete expression
```

`Parser::substituteTemplateParametersWithState` should continue to own
declaration and statement reconstruction, scope management, and the
`TemplateBodySubstitutionState`. When it encounters an expression surface, it
should delegate the whole expression to `ExpressionSubstitutor`, not decide
which expression variants deserve delegation.

Parser-side expansion helpers may still participate where pack deduction,
function-parameter-pack names, aggregate initializer structure, or statement
ownership is required; they must not become a second generic expression
reconstruction path.

#### Environment requirements

The delegating entry point must pass a complete `TemplateEnvironment` rather
than rebuilding incomplete scalar/pack maps at arbitrary call sites. The
environment must preserve:

- declaration order;
- scalar type and non-type bindings;
- template-template bindings;
- empty packs;
- function parameter-pack names and expanded local names;
- enclosing class/member template bindings;
- the current instantiated owner type;
- parent environments used by nested templates.

Where legacy callers still provide `param_map_`, `pack_map_`, and
`template_param_order_`, migrate them incrementally to the existing
`TemplateEnvironment` constructor. Do not remove a legacy constructor until
all callers preserve equivalent binding information.

#### Scope-sensitive expressions

Substitution of local identifiers, lambda parameters, requires expressions,
and definition-bound lookup records depends on lexical state. The migration
must retain the existing `TemplateBodySubstitutionState` and parser scope setup.
Centralizing expression ownership must not mean performing substitution without
the definition context.

#### Suggested migration order

Move low-risk expression families first:

1. literals and explicit leaf nodes;
2. unary, binary, ternary, cast, `sizeof`, and `noexcept` expressions;
3. member access and subscripting;
4. constructor and `new` expressions;
5. direct/member calls and explicit template arguments;
6. lambdas, requires-related expressions, folds, and other scope-sensitive
   forms.

After each family moves, delete the corresponding parser-side expression
branch. Do not leave both paths active as a permanent fallback.

### 4. Separate AST provenance from lifecycle ownership

The boundary checker already runs on initial and pending semantic roots. The
remaining weakness is classification: a concrete materialized function can
still carry `has_template_body_position()` because that position is useful for
provenance, while `PostParseBoundaryChecker` treats that flag as proof the body
is parser-owned and skips it.

Introduce an explicit lifecycle/ownership state on materialized roots or
function bodies. A representative state model is:

```cpp
enum class AstOwnershipPhase : uint8_t {
	ParserPattern,
	ParserDeferredBody,
	ConcreteMaterialized,
	SemaNormalized,
};
```

The exact storage location should follow the existing declaration/body
organization. If a full enum is too invasive initially, add an explicit
`is_concrete_materialized_body` bit and migrate to the enum later. Do not infer
ownership from `has_template_body_position()`, `is_template_pattern()`, or
`is_materialized()` combinations in multiple passes.

Provenance remains separate:

- saved token/body position answers where the body came from;
- template-pattern identity answers which declaration owns the pattern;
- lifecycle state answers which compiler phase owns the current AST.

#### Required transitions

```text
parse template declaration
  ParserPattern or ParserDeferredBody

instantiate/reparse with concrete bindings
  ConcreteMaterialized

run forbidden-node boundary validation
  ConcreteMaterialized (validated)

run semantic normalization successfully
  SemaNormalized

enter IR
  require SemaNormalized
```

Transitions should happen only through the existing registration APIs:

- `registerLateMaterializedTopLevelNode`;
- `registerLateMaterializedTopLevelNodeFront`;
- `registerAndNormalizeLateMaterializedTopLevelNode`;
- their batched normalization path through `takePendingSemanticRoots()`.

Direct appends of instantiated roots to `ast_nodes_` should be eliminated or
asserted against. `appendUserNode` remains for initial non-instantiated roots.

#### Boundary checker change

Refactor `PostParseBoundaryChecker` to use `AstTraversal`/the canonical
expression schema and an ownership policy:

- skip `ParserPattern` and `ParserDeferredBody` bodies;
- inspect every `ConcreteMaterialized` body, even when it has a saved template
  body position;
- inspect parameters, default arguments, exception specifications,
  constructor/base/delegating initializers, and bodies;
- report `FoldExpressionNode` and `PackExpansionExprNode` with the owning path,
  child role, and source location;
- reject the root before `normalizeTopLevelNode` runs. The checker must preserve
  the legitimate deferred-body case until materialization; this is a lifecycle
  distinction, not a blanket ban on pack nodes in every sema traversal.

This makes the pending-root checker effective for the exact failure fixed by
PR #1871 instead of relying on IR's final assertion.

### 5. Make IR consume-only

IR currently contains useful defense-in-depth hard errors such as
`PackExpansionExprNode survived into codegen after pre-sema boundary
enforcement`. Keep these as defense-in-depth, but add a root-level ownership
assertion before expression lowering:

```text
IR root entry
  -> require lifecycle == SemaNormalized
  -> require no parser-only helper nodes
  -> lower using sema-owned types, conversions, and resolved targets
```

The second traversal can be debug-only once lifecycle transitions are proven
reliable, but the lifecycle check should remain in normal builds as an
`InternalError`. Codegen must not call template substitution or invent a pack
expansion based on names or argument counts.

## Implementation phases

### Phase 0: Characterize and lock current behavior

**Status: Complete (2026-08-16).**

- Keep `tests/test_new_expression_pack_expansion_ret0.cpp` as the initial
  regression for allocating and placement `new`.
- Add focused regressions for packs in each arity-changing child position:
  ordinary call arguments, member-call arguments, constructor-call arguments,
  placement arguments, `new` constructor arguments, braced initializer
  elements, and empty packs.
- Include native types of different sizes, structs, nested class templates,
  and member function templates.
- Record the current boundary-check skip behavior with a reduced concrete
  materialized-body regression before changing lifecycle flags.

Exit criterion met for the Phase 1 surfaces. Existing focused regressions plus
`tests/test_expression_child_surfaces_ret42.cpp` cover ordinary/member calls,
constructor and placement arguments, allocating `new`, array bounds, braced
initializers, empty packs, and delegating constructors. A concrete
materialized-body lifecycle regression remains deferred with the lifecycle
state work because this stage does not change ownership flags.

### Phase 1: Add the canonical expression child schema

**Status: Complete (2026-08-16).**

- Create `ExpressionStructure.h`.
- Implement explicit leaf/child classifications for every `ExpressionNode`
  alternative.
- Migrate expression recursion in `AstTraversal`.
- Migrate `PostParseBoundaryChecker::visitExpression` to the shared traversal.
- Add compile-time exhaustiveness enforcement.

Exit criterion met: there is one canonical expression child list, and the
schema's dependent `static_assert` causes a build failure until a new variant
alternative declares its structure or explicit leaf status.

### Phase 2: Add the exhaustive rewriter

**Status: Complete as of 2026-08-16.**

Start by inventorying the existing reconstruction and metadata-copy helpers in
`ExpressionSubstitutor.cpp`, then introduce one-to-one and zero-to-many child
rewriting without changing lifecycle ownership.

- Introduce the one-to-one rewrite and argument flat-map APIs.
- Reuse existing node reconstruction and metadata-copy helpers.
- Move `ExpressionSubstitutor` onto the rewriter family by family.
- Delete the unknown-expression fallback.
- Add direct tests for metadata preservation on resolved calls and qualified
  receiver calls.

Exit criterion: `ExpressionSubstitutor` cannot compile with an unhandled
expression alternative, and all pack child-position regressions pass.

### Phase 3: Remove parser-side expression dispatch duplication

**Status: Complete (2026-08-16).**

- Give `ExpressionSubstitutor` the complete environment and substitution state.
- Delegate every `ExpressionNode` from
  `substituteTemplateParametersWithState`.
- Remove migrated parser branches rather than retaining compatibility
  fallbacks.
- Audit all `ExpressionSubstitutor` construction sites for empty packs, outer
  bindings, and current-owner propagation.

Exit criterion: the parser has one generic expression delegation point and no
second parser-side list of expression variants used merely to choose a
substitution implementation. Parser-owned pack expansion and aggregate/
statement reconstruction may remain where their non-expression context is
required.

### Phase 4: Make lifecycle ownership explicit

**Status: In progress (implementation slice recorded above; exit criteria not
met).**

- Add the ownership state and transitions.
- Separate saved body-position provenance from ownership decisions.
- Change the boundary checker to inspect all concrete materialized bodies.
- Assert that every late materialized root is registered and queued.
- Add the IR root-level `SemaNormalized` requirement.

Exit criterion: deliberately injecting a parser-only helper into a concrete
materialized body fails at the parser/sema boundary with an owning path and
token, before semantic normalization or IR.

### Phase 5: Cleanup and extend reuse

**Status: In progress (implementation slice recorded above; exit criteria not
met).**

- Search for remaining hand-maintained expression child lists in constexpr,
  dependence classification, equivalence, cloning, and diagnostic code.
- Migrate only structural traversal; keep pass-specific semantic decisions in
  their owning pass.
- Remove dead helpers and duplicate recursion after each migration.
- Update `docs/SEMANTIC_ANALYSIS_STATUS.md` with the final lifecycle contract.

Exit criterion: remaining expression-kind switches are intentional semantic
dispatches, documented as such, rather than duplicate structural traversal.

## Test strategy

### Reduced language regressions

At minimum, preserve or add coverage for:

- allocating `new T(args...)` with non-empty and empty packs;
- placement `new (address) T(args...)`;
- a pack in placement arguments if the language form is supported;
- array `new T[count]` where `T` and `count` depend on template parameters;
- nested forwarding calls inside an expanded argument;
- mixed native sizes and an aggregate argument;
- class-template member and free-function-template instantiation;
- a concrete body retaining deferred-body provenance;
- a parser-owned pattern that legitimately retains a pack and is not sent to
  sema early.

Tests for fixed bugs should use `_retX.cpp` naming with return values dependent
on correct behavior. Expected semantic failures should use `_fail.cpp`.

### Existing focused regressions

Run the relevant pack/substitution tests individually during development,
including:

- `test_new_expression_pack_expansion_ret0.cpp`;
- `test_member_call_pack_expansion_ret0.cpp`;
- `test_ctor_direct_init_pack_ret0.cpp`;
- `test_pack_expansion_empty_fn_call_ret42.cpp`;
- `test_aggregate_struct_pack_expansion_ret42.cpp`;
- `test_delegating_ctor_pack_expansion_ret0.cpp`.

### Standard-header probes

Use standard headers only as integration probes after a reduced non-`std`
regression exists. Refresh at least `<any>`, `<deque>`, `<stack>`, and
`<optional>` while this architecture is migrated. Record the first independent
stop and compiler timing in `tests/std/README_STANDARD_HEADERS.md`.

### Full verification

- Run `build_flashcpp.bat` after compiler-source changes.
- Run focused tests serially with the individual test runner.
- Run `pwsh tests/run_all_tests.ps1` after the final rebuild, never in parallel
  with the build.
- Run `git diff --check` and inspect tabs on touched lines.
- Audit compiler-source additions for `std::`, `_MSVC`, vendor-reserved helper
  names, and exact standard-library spellings. Generic library facilities such
  as `std::visit` are expected; compiler behavior must not depend on STL entity
  names from parsed programs.

## Diagnostics

Boundary diagnostics should carry enough data to fix the producer immediately:

```text
Concrete materialized AST contains parser-only PackExpansionExprNode
owner: createValue<long long, short>
child: NewExpression.ConstructorArgument[0]
source: file.cpp:24:41
producer: template body substitution
```

The producer label can initially be coarse. If later materialization APIs record
an origin enum, include it without storing arbitrary strings on every node.

Use `InternalError` when compiler lifecycle metadata is inconsistent or a
concrete body contains an impossible helper node. Use `CompileError` only for an
invalid user program, such as mismatched pack lengths required by the language
rules. Do not turn a compiler ownership failure into a user diagnostic.

## Risks and mitigations

### Rebuilding nodes can lose semantic metadata

Mitigation: inventory and reuse existing reconstruction helpers before moving a
node family. Add tests that inspect behavior requiring preserved selected calls,
return hints, and qualified lookup records.

### Central traversal can become an overly generic semantic framework

Mitigation: keep the canonical layer structural. It names children and rebuilds
them; overload resolution, dependence, constexpr evaluation, and conversion
selection remain in their current owners.

### Pack expansion does not fit one-to-one rewriting

Mitigation: make arity-changing argument rewriting a first-class API from the
start. Do not encode zero/many results in sentinel `ASTNode` values.

### Lifecycle changes can normalize template patterns too early

Mitigation: add explicit parser-pattern/deferred-body tests before changing the
boundary checker. Ownership state transitions must occur only after concrete
bindings have produced a materialized body.

### Large migration obscures regressions

Mitigation: deliver the phases as separate PRs. Each PR should delete a known
duplicate or add one enforced invariant, include reduced regressions, and keep
the full suite green.

## Recommended PR sequence

1. **Expression schema and exhaustive read-only traversal**
   - add the schema;
   - migrate `AstTraversal` and the boundary checker;
   - no substitution behavior change.
2. **Exhaustive expression rewriter**
   - add one-to-one and flat-map reconstruction;
   - migrate low-risk expression families;
   - remove the permissive fallback.
3. **Single substitution owner**
   - route structural expression substitution through `ExpressionSubstitutor`;
   - remove parser-side variant routing.
4. **Explicit materialization lifecycle**
   - add ownership states and boundary enforcement;
   - require sema-normalized roots in IR.
5. **Structural traversal cleanup**
   - migrate other duplicate child walks where reuse is clear;
   - update architecture documentation.

Keeping lifecycle work separate from the rewriter migration makes failures
easier to attribute and avoids combining broad AST reconstruction changes with
late-materialization scheduling changes.

## Definition of done

This plan is complete when:

- `ExpressionNode` has one exhaustive structural schema;
- `ExpressionSubstitutor` has no unknown-expression fallback; intentional
  pass-through of declaration/statement children remains owned by the parser;
- parser template substitution delegates structural expression rewriting to one
  owner, with parser-owned pack/initializer orchestration retained where the
  surrounding statement context requires it;
- structural pack-expanding expression child sequences use one shared flat-map
  mechanism, while parser-owned statement/initializer expansion remains
  explicit;
- concrete materialized bodies are boundary-checked even when they retain
  deferred-template provenance;
- all IR roots are explicitly sema-normalized;
- injecting a new expression alternative breaks the compiler build until its
  structure and rewrite behavior are declared;
- reduced pack-expansion, lifecycle, standard-header, and full-suite tests pass;
- no compiler behavior recognizes standard-library or vendor helper names.
