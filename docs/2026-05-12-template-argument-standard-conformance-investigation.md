# Template Argument Standard-Conformance Investigation

**Date:** 2026-05-12  
**Last updated:** 2026-06-11

This document tracks the standards-facing target for the remaining template
infrastructure work. It should describe the intended semantic model, the
highest-value remaining conformance gaps, and the next tasks required to close
them. It is intentionally not a branch diary.

## Target model

Move FlashCpp toward a template implementation where:

- dependency classification is explicit
- non-dependent lookup remains definition-bound through instantiation
- dependent names preserve semantic identity end-to-end
- substitution, deduction, ranking, and replay/materialization stay separate
- semantic analysis, not parser leftovers, owns final call-target selection in
  normalized flows
- replay succeeds because invariant evidence matches, not because a repair scan
  guessed correctly

## Current conformance baseline

The compiler now has a workable standards-facing baseline in several previously
blocking areas:

- covered non-dependent template-body lookup remains definition-bound
- dependent alias resolution no longer relies on textual reconstruction
- normalized direct-call resolution for template/member calls uses sema-owned
  overload-resolution argument typing
- semantic const-aware lookup for ordinary member calls, `operator[]`, and
  callable `operator()` now rejects stale non-const parser-selected targets
  once sema has typed evidence
- direct-call viability now requires real conversion paths
- indirect call typing preserves the returned object type during overload
  ranking
- replay attachment in the covered routes now expects positive
  identity/signature evidence rather than accepting unresolved
  shape-based matches

## Highest-value remaining standards gaps

### 1. Compatibility recovery is still covering direct-call metadata loss

Some instantiated ordinary direct calls still lose their preserved
definition-bound lookup record while retaining an authoritative mangled target.
Sema now recovers that target, which prevents standards-visible rebinding to a
later overload, but the standards-conforming endpoint is to preserve the
semantic lookup record itself.

Why this matters:

- a preserved mangled target is only a compatibility boundary
- the real semantic model should still know *why* the call is definition-bound
- removing the final parser-selected fallback depends on closing this metadata
  gap first

### 2. Replay must stay invariant-driven

The remaining standards risk is no longer textual dependent-name recovery. It
is replay/materialization paths that may still succeed or sync through weak
name/arity evidence instead of source identity plus canonical substituted
signatures.

Why this matters:

- replay that succeeds through shape-based repair can silently select the wrong
  declaration
- those weak paths tend to reopen non-conforming fallback behavior later in
  sema or codegen

### 3. Final parser-selected ordinary direct-call fallback

The remaining ordinary non-receiver compatibility route should disappear from
normalized flows once replay/materialization preserves enough typed evidence.

Why this matters:

- as long as the fallback exists, sema is not yet the sole owner of final
  ordinary direct-call target selection
- the standard model is cleaner when typed sema evidence resolves or rejects
  the call directly

### 4. Current-instantiation / unknown-specialization coverage

This is still necessary, but it is no longer the immediate next task. Expand
it only for concrete unresolved cases that block steps 1-3.

## Priority order

1. Preserve direct-call lookup records across the remaining replay and
   materialization paths that currently rely on mangled-name recovery.

2. Remove the final parser-selected non-receiver direct-call fallback in
   `resolveCallArgAnnotationTarget(...)` once step 1 is complete for the
   remaining live paths.

3. Tighten the next replay/`StructTypeInfo` sync gap by preserving source
   identity rather than restoring any signature-equivalent or textual repair
   logic.

4. Expand current-instantiation and unknown-specialization handling only where
   it unblocks concrete replay or typed-lookup failures still remaining after
   steps 1-3.

## Standards rules for follow-up work

- do not normalize textual reconstruction as acceptable semantics
- prefer hard failure or proper diagnostics over silent repair in normalized
  semantic flows
- keep compatibility behavior explicit, narrow, and temporary
- treat codegen-side recovery as debt to remove, not as architecture
- when a late retry is still necessary, route it through typed semantic lookup
  instead of rebuilding intent from strings or shallow shape matches

## Validation guidance

For work in this area, rerun:

- the focused regression that motivated the slice
- `template_lookup_non_dependent_no_rebind_ret0.cpp`
- `test_template_dependent_unqualified_mangled_recovery_ret0.cpp`
- `test_template_qualified_direct_call_inner_return_overload_ret0.cpp`
- `test_template_dependent_unqualified_direct_call_nonviable_fail.cpp`
- `test_operator_subscript_sema_receiver_and_arg_overload_ret0.cpp`
- `test_operator_subscript_const_ambiguity_fail.cpp`
- `test_constexpr_operator_bracket_const_nonconst_ret0.cpp`
- `test_subscript_pointer_conversion_template_ret42.cpp`
- full `pwsh tests/run_all_tests.ps1` before considering the slice complete
