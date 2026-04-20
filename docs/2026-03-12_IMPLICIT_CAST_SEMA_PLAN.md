# Semantic-analysis separation plan

> **Status (2026-04-07):** Yes, there is still work left, but the original plan is
> no longer a giant open rewrite. The big migration is mostly complete. What
> remains is a narrower backlog around reference binding, a few intentionally
> retained dependent/unresolved fallbacks, and keeping parser/template boundary
> invariants strict.

This document intentionally replaces the old phase-by-phase implementation log
with a short status summary. The detailed history now lives in git history and
in the follow-up design notes referenced below.

## TODO first: what is actually left

### 1. Finish sema-owned reference binding outside direct/member explicit-argument calls

This is the main remaining language-semantics item still belonging to this plan.

What is already done:

- direct free-function calls and direct member-function calls now classify
  reference binding in sema
- those paths already carry temporary-materialization decisions into codegen
  and full-expression cleanup for materialized struct temporaries

What is still left:

- extend the same sema-owned model to the remaining call/initialization forms
  that still rely on older codegen-local behavior
- keep lifetime cleanup driven by sema-owned decisions, even if codegen remains
  the executor of destructor emission

Long-term decision:

- **reference-binding policy should be decided once in sema**
- codegen should only materialize, destroy, and lower what sema already decided

### 2. Keep constructor and call resolution sema-authoritative for non-dependent code

Most of the constructor/converting-constructor migration is done, but the
remaining fallback behavior should stay confined to unresolved or
template-dependent cases.

What is already done:

- sema owns standard implicit conversions in the main non-dependent expression
  contexts
- sema-selected converting constructors are propagated through the main
  initialization/call/return paths
- constructor overload selection has been centralized much more aggressively than
  when this plan started

What is still left:

- audit the remaining unresolved/template-dependent fallback paths and make sure
  they are explicitly documented as compatibility behavior, not silent
  alternative authority

Long-term decision:

- **for non-dependent code, sema must be the only authoritative source of
  overload/conversion decisions**
- fallback resolution is acceptable only when the AST is genuinely unresolved at
  the time sema runs

### 3. Keep parser/template boundary work separate from this plan

This plan used to mix conversion migration with parser/sema boundary cleanup.
That is no longer the right shape.

What is already done:

- `SemanticAnalysis.cpp` no longer depends on direct
  `parser_.get_expression_type(...)` calls
- empty function-argument pack expansions are consumed during template
  substitution instead of leaking into sema

What is still left:

- remaining fold/pack/pre-sema boundary hardening belongs in
  `docs/2026-03-21_PARSER_TEMPLATE_SEMA_BOUNDARY_PLAN.md`

Long-term decision:

- **do not re-open parser/sema ownership questions inside this document**
- keep this document focused on implicit conversions, constructor selection,
  reference binding, and fallback removal from codegen

### 4. Do not chase more `inferExpressionType` micro-optimizations in isolation

The biggest easy wins were already taken.

Current reality:

- local reinference was reduced materially
- the remaining cost is mostly structural coupling around constructor overload
  typing and phase boundaries, not another obvious local cache opportunity

Long-term decision:

- prefer fixing ownership/canonical data flow over adding more local
  `inferExpressionType` special cases

## Important overall goals

1. **Keep the current pipeline shape:** parse → semantic analysis → IR lowering.
2. **Keep the parser responsible for syntax construction, symbol creation, and
   template-instantiation machinery.**
3. **Keep sema responsible for non-dependent language semantics that need type,
   value-category, overload, and initialization context.**
4. **Make codegen consume semantic decisions rather than rediscover them.**
5. **Move toward one authoritative owner per semantic fact.**

## What is already done

The large majority of the original migration goal is implemented.

### Infrastructure

- `SemanticAnalysis::run()` exists as a real post-parse, pre-IR pass
- expression semantic slots and canonical type interning exist
- the pass is timed separately and has enough seam/instrumentation to remain a
  first-class stage

### Standard implicit conversions

Sema now owns or strongly drives standard conversion annotation for the main
non-dependent cases, including:

- return conversions
- function-call argument conversions
- member-function argument conversions
- constructor argument conversions
- variable/declaration initialization
- simple assignment
- compound assignment
- builtin binary arithmetic/comparison conversions
- shift promotions
- unary promotions
- ternary common-type conversions
- contextual `bool`

### User-defined and constructor-related progress

The plan originally treated these as late follow-up work. That is now outdated.
The codebase has moved much farther:

- sema-owned `UserDefined` conversion coverage exists for conversion operators
- copy-initialization converting constructors are covered across the major
  variable/call/return paths
- selected-constructor tracking is no longer declaration-only
- constructor overload resolution has been substantially centralized compared to
  the original design

### Boundary hardening

- sema no longer relies on broad direct parser expression-type queries
- fold/pack helper nodes are treated as boundary concerns instead of normal sema
  input

## Architecture decisions that should now be treated as settled

### 1. Use annotation-first semantics, not a second full semantic tree

Keep compact semantic slots plus side storage / interned handles.

Do **not**:

- clone the whole AST
- attach large by-value semantic payloads to every expression node
- build a parallel full semantic tree just to model ordinary implicit casts

### 2. Keep sema narrow and authoritative

The right target is still a **post-parse semantic normalization pass**, not a
full “move all parser semantics out of the parser” rewrite.

That means:

- parser stays parser-shaped
- sema owns semantic normalization for non-dependent constructs
- codegen reads sema results

### 3. One owner per fact

These decisions should not be made independently in multiple stages anymore:

- implicit standard conversions
- contextual-`bool` classification
- non-dependent converting-constructor selection
- non-dependent reference-binding classification
- overload choice once the call/constructor is fully known

### 4. Keep dependent/unresolved fallback explicit

Dependent/template-delayed code is a legitimate reason to retain fallback paths.
It is **not** a reason to keep duplicate policy engines for ordinary code.

Required rule:

- every fallback path should be explainable as “sema could not yet know,” not
  “codegen also has its own opinion”

### 5. Diagnostics should keep moving earlier

Long term, semantic/type-law diagnostics should continue to move toward sema:

- overload viability / ambiguity
- invalid implicit conversions
- narrowing / initialization diagnostics
- constant-expression-required diagnostics

Codegen should not be the place where ordinary type-law mistakes are discovered.

## Recommended division of future work

### Still in scope for this plan

- broader reference-binding coverage
- keeping non-dependent converting-constructor and conversion decisions
  sema-authoritative
- removing or tightening the last non-dependent compatibility fallbacks

### Moved to other architectural tracks

- parser/template boundary hardening:
  `docs/2026-03-21_PARSER_TEMPLATE_SEMA_BOUNDARY_PLAN.md`
- TempVar/reference-materialization execution details:
  `docs/2026-03-24_TEMPVAR_LOCAL_VARIABLE_IR_ARCHITECTURE_PROPOSAL.md`

## Bottom line

**Is there work left? Yes.**

But the honest answer is:

- the big semantic-pass migration succeeded
- this is no longer a giant “invent sema” project
- the remaining work is focused and architectural:
  - finish reference-binding ownership
  - keep non-dependent overload/conversion choice sema-authoritative
  - keep parser/template boundary work separate
  - remove only the fallbacks that should no longer exist

That is the right long-term compiler architecture for FlashCpp: **parser builds,
sema decides, codegen lowers**.
