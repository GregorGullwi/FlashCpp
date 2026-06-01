# Template Argument Standard-Conformance Investigation

**Date:** 2026-05-12  
**Last updated:** 2026-06-01

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
- dependent alias resolution is semantic-only: the textual fallback in
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
  treats token/name shape fallback as a valid equivalence
- `materializeAliasTemplateInstance` now falls back to
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
than name/arity fallback scans, and expand current-instantiation /
unknown-specialization modeling only where it unblocks those paths.

## Priority order

1. ~~Preserve semantic owner/member-chain records in the remaining dependent
   alias callers.~~ **Done** — the textual recovery path is removed.

2. Continue tightening replay attachment so valid cases succeed via source
   identity plus canonical substituted-signature evidence, not fallback scans.
   The next slice is likely the remaining unresolved-signature acceptance paths
   where replay can still succeed without enough evidence outside the now-
   tightened plain-member and member-template paths.

3. Expand current-instantiation, dependent-base, and unknown-specialization
   handling only where that unblocks step 2.

4. Leave broader cleanup for later unless it becomes a blocker:
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

## Validation guidance

For work in this area:

- add a focused regression first when a new gap is identified
- rerun the three former dependent-alias blocker tests when touching alias
  ownership or current-instantiation handling (they now exercise the
  semantic-only route)
- rerun the dependent member-template static constexpr regressions when touching
  nested owner/member-chain replay or static member initializer materialization
- run `pwsh tests/run_all_tests.ps1` before considering the slice complete
