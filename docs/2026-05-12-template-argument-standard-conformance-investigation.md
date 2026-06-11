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
- resolved direct-call materialization now preserves
  `DependentUnqualifiedCallLookupRecord` both when the instantiated target
  stays definition-bound and when dependent-unqualified POI completion selects
  a different final function, reducing replay-heavy dependence on parser
  reruns and mangled-name recovery in those paths
- the final parser-selected non-receiver direct-call fallback in
  `resolveCallArgAnnotationTarget(...)` is gone; ordinary direct calls now
  resolve through semantic metadata, typed lookup, or explicit unresolved
  terminals instead of reusing the parser-selected target late
- primary-template out-of-line constructor replay now synchronizes the
  `StructTypeInfo` constructor copy through preserved source-member identity
  when that identity is already known, instead of recovering it afterward

## Highest-value remaining standards gaps

### 1. Replay must stay invariant-driven

The remaining standards risk is no longer textual dependent-name recovery. It
is replay/materialization paths that may still succeed or sync through weak
name/arity evidence instead of source identity plus canonical substituted
signatures.

Why this matters:

- replay that succeeds through shape-based repair can silently select the wrong
  declaration
- those weak paths tend to reopen non-conforming fallback behavior later in
  sema or codegen

Near-term remaining scope:

- in-loop member-function / constructor registration inside
  `Parser::try_instantiate_class_template()` is now unified locally, so the
  next cleanup target in this area is the remaining out-of-line replay/sync
  helper duplication rather than more registration drift
- top-level replay-driven `StructTypeInfo` sync now uses shared
  identity-first helpers for plain members and constructors; the remaining
  duplication is in nested/member-template-specific attachment paths that do
  not all route through those helpers yet
- partial-specialization constructor copies now use the same
  source-member-to-`StructTypeInfo` index map as the primary-template path,
  and the nested/member-template replay sites that already preserve a matched
  source declaration now route through the shared identity-first helpers too
- partial-specialization nested member-template replay now also syncs the
  `StructTypeInfo` copy through the shared identity-first helper once the
  matched source declaration is preserved
- remaining member-template and constructor-template `StructTypeInfo` sync
  sites that still route through replay-source-key helper scans after the
  matched source declaration is already known

### 2. Compatibility recovery is still covering direct-call metadata loss outside dependent-unqualified completion

Dependent-unqualified replay/materialization now preserves both the
definition-bound ordinary lookup and the final point-of-instantiation target.
The remaining compatibility risk is therefore narrower: other instantiated
ordinary direct calls can still lose structured semantic evidence and fall back
to mangled-name recovery instead of carrying their final lookup result
directly.

Why this matters:

- a preserved mangled target is only a compatibility boundary
- the real semantic model should still know why the call is definition-bound
  and, when POI completion changes the winner, which final semantic target was
  selected

### 3. Current-instantiation / unknown-specialization coverage

This is still necessary, but it is no longer the immediate next task. Expand
it only for concrete unresolved cases that block steps 1-2.

## Priority order

1. Tighten the next member-template or constructor-template
   `StructTypeInfo` sync gap by preserving source identity rather than routing
   through a helper scan after the source declaration is already known.

2. Tighten the next remaining mangled-name compatibility path by preserving
   structured direct-call metadata in the owning replay/materialization path
   instead of relying on mangled recovery.

3. Expand current-instantiation and unknown-specialization handling only where
   it unblocks concrete replay or typed-lookup failures still remaining after
   steps 1-2.

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
- `test_template_dependent_unqualified_member_replay_ret0.cpp`
- `test_template_dependent_unqualified_poi_adl_record_ret42.cpp`
- `test_template_qualified_direct_call_inner_return_overload_ret0.cpp`
- `test_template_dependent_unqualified_direct_call_nonviable_fail.cpp`
- `test_operator_subscript_sema_receiver_and_arg_overload_ret0.cpp`
- `test_operator_subscript_const_ambiguity_fail.cpp`
- `test_constexpr_operator_bracket_const_nonconst_ret0.cpp`
- `test_subscript_pointer_conversion_template_ret42.cpp`
- full `pwsh tests/run_all_tests.ps1` before considering the slice complete
