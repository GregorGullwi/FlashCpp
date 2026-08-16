# 2026-08-16 Exhaustive Expression Rewrite and Sema Boundary Plan

## Status

Phase 0, Phase 1, and Phase 2 are complete as of 2026-08-16. Phases 3-5
remain pending.
This document describes follow-up work after PR #1871, which added the missing
`NewExpressionNode` template-substitution path. The plan is intentionally
broader than that fix: its goal is to make the same class of omission difficult
to compile, easy to diagnose, and impossible to reach code generation silently.

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
  structural expression families onto the rewriter; its unknown-expression
  fallback now fails at the dynamic boundary, while non-expression statement
  and declaration children remain owned by their existing parser passes.
- Added reduced metadata and pack-sequence regressions in
  `test_expression_rewriter_metadata_ret42.cpp` and
  `test_expression_rewriter_pack_sequences_ret42.cpp`.

Validation for this stage: the MSVC build completed with 0 warnings/errors;
all focused regressions passed; the `<any>` and `<deque>` probes reached the
same documented out-of-scope `_Seek_to`/`view_interface::operator==` stops;
the full suite passed 2,889 regular tests and 247 expected-failure tests; and
the compiler-source audit found no standard-library or vendor-name behavior
special cases. The non-dependent receiver lookup gap remains documented and
unchanged. Parser-side expression dispatch consolidation remains Phase 3, and
explicit lifecycle ownership remains Phase 4.

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
- Parser-only helper nodes must never reach a sema-normalized body or IR.
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
3. **Pack invariant:** `FoldExpressionNode` and `PackExpansionExprNode` may exist
   only on parser-owned template-pattern/deferred surfaces. They may not exist
   on a concrete materialized body presented to sema.
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

Remove this behavior from `ExpressionSubstitutor::substitute`:

```cpp
// For any other node type, return as-is
return expr;
```

Explicit leaf nodes may be returned unchanged. An unrecognized expression node
must be a compile-time error in the visitor or an `InternalError` only at a
truly dynamic boundary that cannot be made exhaustive.

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
- reject the root before `normalizeTopLevelNode` runs.

This makes the pending-root checker effective for the exact failure fixed by
PR #1871 instead of relying on IR's final assertion.

### 5. Make IR consume-only

IR currently contains useful hard errors such as
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

- Give `ExpressionSubstitutor` the complete environment and substitution state.
- Delegate every `ExpressionNode` from
  `substituteTemplateParametersWithState`.
- Remove migrated parser branches rather than retaining compatibility
  fallbacks.
- Audit all `ExpressionSubstitutor` construction sites for empty packs, outer
  bindings, and current-owner propagation.

Exit criterion: the parser has one expression delegation point and no list of
individual expression variants to route.

### Phase 4: Make lifecycle ownership explicit

- Add the ownership state and transitions.
- Separate saved body-position provenance from ownership decisions.
- Change the boundary checker to inspect all concrete materialized bodies.
- Assert that every late materialized root is registered and queued.
- Add the IR root-level `SemaNormalized` requirement.

Exit criterion: deliberately injecting a parser-only helper into a concrete
materialized body fails at the parser/sema boundary with an owning path and
token, before semantic normalization or IR.

### Phase 5: Cleanup and extend reuse

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
   - route all expression substitution through `ExpressionSubstitutor`;
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
- `ExpressionSubstitutor` has no unknown-node fallback;
- parser template substitution delegates every expression to one owner;
- pack-expanding child sequences use one shared flat-map mechanism;
- concrete materialized bodies are boundary-checked even when they retain
  deferred-template provenance;
- all IR roots are explicitly sema-normalized;
- injecting a new expression alternative breaks the compiler build until its
  structure and rewrite behavior are declared;
- reduced pack-expansion, lifecycle, standard-header, and full-suite tests pass;
- no compiler behavior recognizes standard-library or vendor helper names.
