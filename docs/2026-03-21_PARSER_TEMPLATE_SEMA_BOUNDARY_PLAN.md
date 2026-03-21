# Parser / Template-Substitution / Sema Boundary Plan

## Problem

FlashCpp already runs template instantiation during parsing and runs semantic
analysis after parsing, but the ownership boundary between those phases is still
blurred.

Today that shows up in two ways:

- parser/template-only expression forms such as `FoldExpressionNode` and
  `PackExpansionExprNode` are still visible enough that sema and codegen carry
  defensive handling for them
- semantic analysis still relies on `parser_.get_expression_type(...)` in a
  number of places instead of using sema-owned sources of truth

This is not just a cleanup issue. It makes the pipeline harder to reason about,
encourages fallback behavior across phase boundaries, and weakens invariants
that should be explicit.

## Current pipeline reality

The current high-level pipeline is already:

1. parse source
2. instantiate/substitute templates during parsing
3. run semantic analysis
4. lower to IR / codegen

Key evidence in the current tree:

- `src\FlashCppMain.cpp`
	- parsing comment: template instantiation also happens during parsing
	- semantic analysis runs post-parse, pre-IR
	- deferred generation explicitly notes template instantiations do **not**
	  happen there
- `src\Parser_Templates_Substitution.cpp`
	- expands fold expressions and pack expansions during substitution
- `src\IrGenerator_Expr_Primitives.cpp`
	- treats surviving `FoldExpressionNode` / `PackExpansionExprNode` as internal
	  errors that should already have been eliminated
- `src\SemanticAnalysis.cpp`
	- still contains parser fallback paths via `parser_.get_expression_type(...)`

So the architectural problem is **not** that template substitution runs after
sema. The problem is that sema is not yet fully insulated from parser-owned and
template-substitution-owned forms.

## Goals

1. Define which AST forms are legal after parsing/template substitution and
   before sema.
2. Ensure template-only expression nodes are eliminated, expanded, or diagnosed
   before sema/IR.
3. Reduce sema dependence on live parser type queries.
4. Make phase ownership explicit enough that future work can rely on it.

## Non-goals

- Do not combine this work with the broader migration of user-defined
  conversions, reference binding, or lifetime extension out of codegen.
- Do not refactor all parser internals at once.
- Do not re-implement template substitution in sema.

## Desired invariant

After `Parser::parse()` returns and before `SemanticAnalysis::run()` starts:

- fold expressions are already expanded into ordinary expression trees, or are
  rejected earlier with a targeted diagnostic if expansion cannot happen
- pack expansion expression nodes do not survive in ordinary expression
  contexts; if they do, that is a parser/substitution bug or an unsupported
  parser feature that should be diagnosed before sema
- sema can infer or read expression types without having to ask the parser to
  recompute them on demand in common paths

## Phase 1 status

This document's Phase 1 slice is now implemented as inventory + guardrails only.

- the legal sema-owned post-parse expression surface is now documented in code
  comments near `ExpressionNode`, `TemplateParameterReferenceNode`,
  `FoldExpressionNode`, and `PackExpansionExprNode`
- a lightweight checker now runs at the sema entry point and walks the
  sema-owned post-parse AST surface before normalization begins
- that checker reports surviving `FoldExpressionNode` /
  `PackExpansionExprNode` instances as boundary violations, but deliberately
  keeps the existing bridge/fallback behavior in place for now
- current `SemanticAnalysis.cpp` uses of `parser_.get_expression_type(...)` are
  now inventory-tagged in code with brief classification comments

### Legal post-parse / pre-sema expression-node surface (Phase 1)

For Phase 1, the legal surface is defined in terms of the AST that sema is about
to own and walk, not every parser-internal template artifact that may still be
stored elsewhere in parser-owned state.

- legal on the sema-owned surface:
  - ordinary `ExpressionNode` variants that represent runtime or sema-visible
    expressions
  - `TemplateParameterReferenceNode`, which is still a supported surviving
    template-related node in sema/codegen today
- forbidden on the sema-owned surface:
  - `FoldExpressionNode`
  - `PackExpansionExprNode`
- intentionally out of scope for the Phase 1 checker:
  - parser-owned template declarations / deferred template bodies that sema does
    not yet walk directly

If fold or pack-expansion helper nodes are found on the sema-owned surface after
parsing, that is now treated as an explicit boundary violation report instead of
an undocumented accidental fallback.

## Phase 2 progress

Phase 2 is now implemented for both boundary-helper node kinds:

- surviving `FoldExpressionNode` on the sema-owned post-parse surface is no
  longer treated as a permissive sema typing fallback
- surviving `PackExpansionExprNode` on that same surface is now diagnosed
  before semantic normalization instead of being left to late codegen
  defensive handling
- `SemanticAnalysis::normalizeExpression(...)` and
  `SemanticAnalysis::inferExpressionType(...)` now treat surviving folds as
  unreachable invariant violations instead of consulting
  `parser_.get_expression_type(...)`
- `SemanticAnalysis::normalizeExpression(...)` now also treats surviving pack
  expansions as unreachable once the boundary check has run

## Phase 3 progress

Phase 3 has started with two low-risk local fallback removals:

- `inferExpressionType(LambdaExpressionNode)` now relies only on the parser's
  immediate lambda-closure registration in `gTypesByName`
- `tryResolveCallableOperator()` now uses sema-owned inference exclusively when
  trying to build overload-resolution argument types
- constructor-overload argument typing now goes through a shared sema-first
  helper, with parser fallback retained only for the remaining parser-owned
  lookup facts
- `inferExpressionType(QualifiedIdentifierNode)` now mirrors the parser's
  namespace/static-member/enum lookup directly in sema, including lazy static
  member instantiation for struct-qualified names
- `inferExpressionType(IdentifierNode)` now recovers globals/functions/static
  members/enumerator declarations from sema's `symbols_` plus AST binding
  metadata; parser fallback remains only for implicit-member/unresolved cases
- the broad parser fallbacks in auto-return deduction and constructor
  overload-argument typing now route through a shared helper that only permits
  parser typing for `TemplateParameterReferenceNode` and unresolved
  `IdentifierNode` forms that still need parser-owned context
- instantiated `FunctionDeclarationNode` outer template bindings now seed sema's
  local type scope before body normalization, allowing
  `TemplateParameterReferenceNode` to resolve locally in those function bodies
- instantiated constructors and destructors now carry the same outer template
  binding metadata and seed sema scope before body normalization, shrinking the
  remaining `TemplateParameterReferenceNode` bridge to contexts without that
  metadata path
- instantiated lambdas now carry enclosing outer-template bindings into both
  direct sema lambda normalization and deferred generic-lambda normalization,
  further shrinking the remaining `TemplateParameterReferenceNode` bridge
- instantiated variable declarations now carry outer-template bindings so sema
  can normalize their initializers without falling back to parser-owned
  template-parameter typing in those contexts
- sema's `resolveRemainingAutoReturnsInNode()` now seeds function outer-template
  bindings before deducing `auto` returns, so instantiated function returns that
  mention surviving `TemplateParameterReferenceNode`s can resolve sema-first too

## Workstreams

### Workstream 1: make post-parse AST legality explicit

Add a clear definition of which expression nodes are:

- parser-only
- allowed in sema
- allowed in IR/codegen

At minimum this list should cover:

- `FoldExpressionNode`
- `PackExpansionExprNode`
- `TemplateParameterReferenceNode`
- any other template-substitution helper nodes that are intended to disappear

This should be documented in code comments near the AST node definitions and in
the sema/codegen entry points that currently carry defensive fallbacks.

### Workstream 2: enforce elimination of template-only expression nodes earlier

#### FoldExpressionNode

Current behavior:

- substitution expands fold expressions
- sema now fails before normalization if a fold survives on the sema-owned
  surface
- codegen still hard-fails if a fold survives outside that guarded path

Required next step:

- audit the remaining parser-owned paths where a fold can still exist outside
  the sema-owned surface
- decide whether any surviving cases should become earlier parser/substitution
  diagnostics instead of invariant failures

#### PackExpansionExprNode

Current behavior:

- expansion logic is context-specific
- sema now rejects surviving pack expansions before normalization on the
  sema-owned surface
- codegen still hard-fails if a pack expansion survives outside that guarded
  path

Required next step:

- audit creation sites and expansion sites for remaining parser-owned contexts
- tighten parser/substitution-time diagnostics where a surviving pack expansion
  can be diagnosed more locally than the sema boundary

### Workstream 3: reduce `parser_.get_expression_type(...)` inside sema

This is the bigger architectural task.

First, inventory every current parser fallback and classify it:

- truly temporary bridge
- parser-owned fact that should instead be attached to the AST during parsing
- sema-owned fact that should be inferable from child nodes and existing
  side tables

Likely buckets:

- identifier/global/qualified-id fallback
- function-call return-type fallback
- template-parameter-reference fallback
- remaining expression-node gaps
- constructor / overload-resolution driven type lookups

Then migrate those buckets one at a time:

1. eliminate easy local inference gaps
2. prefer resolved AST annotations over live parser recomputation
3. leave only narrow fallback sites that are explicitly documented

#### Phase 1 inventory of current `SemanticAnalysis.cpp` parser fallbacks

| Site | Current purpose | Phase 1 classification |
| --- | --- | --- |
| `deducePlaceholderReturnType()` | recover return-expression type for `auto` / `decltype(auto)` deduction when sema inference cannot yet supply it | narrowed auto-return bridge: function/lambda deduction now sees outer-template bindings before using the documented fallback |
| `inferExpressionType(IdentifierNode)` | recover types for non-local identifiers outside sema's local scope stack | narrowed bridge: sema-native for globals/functions/static members/enumerators; parser fallback remains for implicit-member/unresolved identifiers |
| `inferExpressionType(TemplateParameterReferenceNode)` | recover instantiated template-parameter value types not visible through local sema scope alone | narrowed bridge: function/ctor/dtor/lambda/variable outer-template bindings now seed sema scope; parser fallback remains for contexts without that metadata |
| `tryAnnotateConstructorCallArgConversions()` | build constructor overload-resolution argument types | constructor-overload bridge (now sema-first) |
| `tryAnnotateInitListConstructorArgs()` | build constructor overload-resolution argument types for braced initialization | constructor-overload bridge (now sema-first) |

### Workstream 4: add an explicit post-parse invariant check

Introduce a lightweight validation pass, or a debug-only verification mode,
between parsing and sema that walks the AST and flags forbidden surviving
template-only nodes.

This pass should initially be non-invasive:

- log or count surviving illegal nodes
- optionally gate harder failures behind a debug flag

Once confidence is high, tighten it so illegal post-parse nodes fail earlier and
more locally than IR/codegen.

### Workstream 5: tests and rollout discipline

For each slice:

- add or reuse focused regression tests that exercise the affected template form
- validate with targeted tests first
- rerun the full Windows suite after changes that affect sema or template
  substitution behavior

Important coverage areas:

- existing fold tests
- existing pack-expansion-in-function-call tests
- tests that intentionally probe unsupported contexts, if they exist or are
  added later

## Suggested implementation order

### Phase 1: inventory and guardrails

Implemented in this slice:

- documented legal post-parse expression forms for the sema-owned surface
- audited `parser_.get_expression_type(...)` usage in `SemanticAnalysis.cpp`
- added a narrow invariant checker for surviving fold/pack nodes

### Phase 2: seal template-only nodes

- tighten fold-expression handling from permissive fallback toward explicit
  invariant enforcement **(implemented)**
- add earlier detection/diagnostics for surviving `PackExpansionExprNode`
  **(implemented at the pre-sema boundary)**

### Phase 3: migrate sema off parser fallbacks

- remove the easiest sema parser fallbacks first
- convert parser-derived facts into AST annotations or sema-owned lookups
- keep performance in mind for hot paths

### Phase 4: tighten hard boundaries

- make surviving parser/template-only expression nodes a pre-sema failure in
  normal builds once the tree is clean
- reduce codegen defensive handling to assertion-only paths if appropriate

## Exit criteria

This plan is complete when:

- the legal post-parse AST surface is documented
- surviving `PackExpansionExprNode` is treated as an invariant violation or an
  earlier targeted diagnostic
- `FoldExpressionNode` no longer needs parser-fallback behavior in normal flow
- the number of sema `parser_.get_expression_type(...)` dependencies is reduced
  to a small, explicit, justified set
- sema/codegen ownership is clear enough that new work does not add fresh
  parser-to-sema fallback paths casually

## Why this is separate from the implicit-cast roadmap

`docs\2026-03-12_IMPLICIT_CAST_SEMA_PLAN.md` is still the right place for the
ongoing implicit-cast and sema-lowering rollout.

This document tracks a broader architectural cleanup:

- parser ownership
- template-substitution ownership
- sema ownership
- post-parse AST invariants

That is related work, but it is no longer just an implicit-cast follow-up.
