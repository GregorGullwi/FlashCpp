# Template Argument Standard-Conformance Investigation

**Date:** 2026-05-12  
**Last updated:** 2026-05-30

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
- dependent alias resolution already prefers semantic owner/member-chain
  re-entry before the remaining textual fallback
- `materializeAliasTemplateInstance` now falls back to
  `materializeInstantiatedMemberAliasTarget` for direct-parameter alias cases
  where the instantiated name resolves to a member type

## Highest-value remaining standards gap

The next conformance step is not a broad rewrite. It is deleting the last
textual dependent-alias recovery path by preserving semantic records in the
remaining recordless callers.

Current blockers:

- `test_member_template_alias_preserves_outer_metadata_ret0.cpp`
- `test_alias_template_nested_member_value_ret42.cpp`
- `test_template_current_instantiation_alias_qualified_deeper_member_ret0.cpp`

A removal probe in this slice confirmed these are the only three remaining
dependents. The specific failure cause: each test's `TypeInfo` is missing a
`DependentQualifiedNameRecord` at deduction time, so the semantic path returns
`nullopt` and the textual path is entered. The next step is to trace each
pattern through parsing and materialization to find where the record is dropped
or never populated.

## Priority order

1. Preserve semantic owner/member-chain records in the remaining dependent
   alias callers.

2. Continue tightening replay attachment so valid cases succeed via source
   identity plus canonical substituted-signature evidence, not fallback scans.

3. Expand current-instantiation, dependent-base, and unknown-specialization
   handling only where that unblocks steps 1 or 2.

4. Leave broader cleanup for later unless it becomes a blocker:
   sema-owned ranking/deduction expansion, remaining repair-path removal, and
   sema-level modeling for aggregate-forwarding constructor sequences.

## Standards rules for follow-up work

- do not normalize textual reconstruction as acceptable semantics
- prefer invariant failures or proper diagnostics over silent repair in
  normalized flows
- keep compatibility behavior explicit, narrow, and temporary
- treat codegen-side recovery as debt to remove, not a design tool

## Validation guidance

For work in this area:

- add a focused regression first when a new gap is identified
- rerun the three residual dependent-alias blocker tests when touching alias
  ownership or current-instantiation handling
- run `pwsh tests/run_all_tests.ps1` before considering the slice complete
