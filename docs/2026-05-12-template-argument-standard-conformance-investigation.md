# Template Argument Standard-Conformance Investigation

**Date:** 2026-05-12  
**Last updated:** 2026-06-02

This document tracks the standards-facing endpoint for template argument and
dependent-name work. It should describe the intended model and the shortest path
from the current implementation to that model, not a full branch history.

## Goal

Move FlashCpp toward a sema-owned template system where:

- dependency classification is explicit
- template arguments are classified against the actual target parameter
- dependent names preserve semantic identity end-to-end
- substitution, deduction, ranking, and materialization stay separate
- replay-heavy paths become invariant-driven instead of repair-driven

## What is already true enough to build on

- semantic lookup records cover the main template lookup paths
- covered non-dependent template-body lookup preserves definition-context
  binding
- several replay paths already preserve source identity and enough context to
  avoid AST-only intent recovery
- owner/member-template chains are preserved semantically in many covered alias
  and replay cases
- explicit member-template call handling now carries enough early argument
  information to avoid wrong cached overload reuse
- semantic direct-call resolution for normalized template/member calls now uses
  sema-owned overload-resolution argument typing instead of parser-owned
  argument collection, so dependent out-of-line member overloads are selected
  from the concrete substituted call signature seen during semantic analysis
- dependent alias resolution is semantic-only: the textual recovery path in
  `resolveDependentMemberAlias(...)` has been removed in favor of preserved
  owner/member-chain records and instantiation-context bindings
- dependent nested member-template static constexpr chains now reuse typed
  owner/member-chain records, active template bindings, and inherited
  member-template lookup instead of hard-coded constexpr name scans
- out-of-line member replay attachment now fails instantiation when replay
  identity plus substituted-signature evidence cannot attach the definition,
  instead of silently continuing into later template instantiation
- plain out-of-line member replay now also requires positive signature
  evidence for single-candidate same-name attachment; unresolved
  substituted-signature outcomes no longer attach by default
- nested and partial-specialization constructor-template replay now fails
  directly when replay identity plus substituted-signature evidence cannot
  attach the out-of-line definition
- replay signature matching now reports explicit
  `Match`/`Mismatch`/`InsufficientEvidence` outcomes; attachment paths treat
  only `Match` as valid evidence
- replay attachment loops that previously flattened matching to boolean now
  preserve `InsufficientEvidence` and fail instantiation directly for
  nested/partial member-template and constructor-stub routes
- plain out-of-line member replay now also preserves `InsufficientEvidence`
  and fails instantiation directly instead of degrading to generic
  replay-attachment misses
- constructor replay sync into StructTypeInfo now requires
  `typeSpecifiersMatchForSignatureValidation(...)` evidence and no longer
  treats token/name shape equivalence as valid evidence
- `materializeAliasTemplateInstance` now delegates to
  `materializeInstantiatedMemberAliasTarget` for direct-parameter alias cases
  where the instantiated name resolves to a member type
- member type aliases preserve surface modifiers (pointer/reference/cv/array/
  function) across alias chains rather than collapsing to the terminal type

## Highest-value remaining standards gap

The textual dependent-alias recovery path has been deleted; the former blockers
now resolve through preserved semantic records:

- `test_member_template_alias_preserves_outer_metadata_ret0.cpp`
- `test_alias_template_nested_member_value_ret42.cpp`
- `test_template_current_instantiation_alias_qualified_deeper_member_ret0.cpp`

With textual recovery gone, the next conformance step is keeping replay
attachment invariant-driven: tighten the remaining replay paths so valid cases
succeed via source identity plus canonical substituted-signature evidence rather
than name/arity repair scans, and expand current-instantiation /
unknown-specialization modeling only where it unblocks those paths.

## Priority order

1. ~~Preserve semantic owner/member-chain records in the remaining dependent
   alias callers.~~ **Done** — the textual recovery path is removed.

2. Continue tightening replay attachment so valid cases succeed via source
   identity plus canonical substituted-signature evidence, not shape-based
   repair scans.
   The next slice is likely the remaining unresolved-signature acceptance paths
   where replay can still succeed without enough evidence outside the now-
   tightened plain-member and member-template paths.

3. Unify the remaining semantic call-resolution entry points around the same
   sema-owned overload-resolution argument collector now used for normalized
   template/member direct calls. This is the shortest path to preventing stale
   parser-selected targets from surviving semantic normalization.

4. Expand current-instantiation, dependent-base, and unknown-specialization
   handling only where that unblocks steps 2 and 3.

5. Leave broader cleanup for later unless it becomes a blocker:
   sema-owned ranking/deduction expansion, remaining repair-path removal, and
   sema-level modeling for aggregate-forwarding constructor sequences.

## Standards rules for follow-up work

- do not normalize textual reconstruction as acceptable semantics
- prefer invariant failures or proper diagnostics over silent repair in
  normalized flows
- keep compatibility behavior explicit, narrow, and temporary
- treat codegen-side recovery as debt to remove, not a design tool; when a
  late retry is still needed, it should delegate to typed lookup such as
  `resolveBaseClassMemberTypeChain`

## Next steps

1. Apply the sema-owned overload-resolution argument collector to the remaining
   semantic call-resolution sites that still rely on parser-owned expression
   typing or reduced argument modeling.

2. Continue deleting replay-attachment acceptance paths that still allow
   insufficient substituted-signature evidence outside the now-hardened member
   and constructor routes.

3. Only after those are stable, extend current-instantiation and
   unknown-specialization modeling for the specific unresolved cases that remain.

## 2026-06-02 constexpr lambda conformance note

The updated constexpr-lambda tests exposed runtime capture-lowering bugs rather
than compile-time evaluation failures. Generated code now follows the C++20
closure model for captured `this` and nested captures:

- `[this]` member calls use the captured enclosing object pointer
- `[*this]` member calls use the copied object stored in the closure
- nested `[this]` captures propagate the effective enclosing object pointer,
  including when the enclosing lambda captured `*this`
- nested reference captures of enclosing closure members take the member's
  address directly and preserve the referenced object's real size
- constexpr member evaluation now preserves receiver cv-qualification through
  synthetic `this` bindings and rejects stale lowered non-const targets when
  evaluating calls from a const member body

Validated with all `tests/*constexpr_lambda*.cpp` tests on 2026-06-02.

## Validation guidance

For work in this area:

- add a focused regression first when a new gap is identified
- rerun the three former dependent-alias blocker tests when touching alias
  ownership or current-instantiation handling (they now exercise the
  semantic-only route)
- rerun the dependent member-template static constexpr regressions when touching
  nested owner/member-chain replay or static member initializer materialization
- run `pwsh tests/run_all_tests.ps1` before considering the slice complete
