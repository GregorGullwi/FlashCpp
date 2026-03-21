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
- codegen hard-fails if a fold survives
- sema now has an explicit parser-fallback case as a defensive bridge

Required next step:

- audit the remaining paths where a fold can reach sema
- keep the current sema fallback only as a transition aid
- once the invariant is proven, tighten that path into an assertion or targeted
  early diagnostic instead of relying on parser fallback

#### PackExpansionExprNode

Current behavior:

- expansion logic is context-specific
- codegen hard-fails if a pack expansion survives
- sema has no meaningful direct typing story for it

Required next step:

- treat this primarily as an invariant/enforcement problem, not an
  `inferExpressionType(...)` feature gap
- audit creation sites and expansion sites
- add early diagnostics or invariant checks for surviving pack expansions in
  unsupported contexts

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

- document legal post-parse expression forms
- audit `parser_.get_expression_type(...)` usage in `SemanticAnalysis.cpp`
- add a narrow invariant checker for surviving fold/pack nodes

### Phase 2: seal template-only nodes

- tighten fold-expression handling from permissive fallback toward explicit
  invariant enforcement
- add earlier detection/diagnostics for surviving `PackExpansionExprNode`

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
