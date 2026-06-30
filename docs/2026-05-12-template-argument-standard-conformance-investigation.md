# Template Argument Standard-Conformance Investigation

**Date:** 2026-05-12  
**Last updated:** 2026-06-30

This document tracks the standards-facing target for the remaining template
infrastructure work. Keep it focused on the intended semantic model, active
conformance gaps, and the next concrete tasks. Do not use it as a branch log.

## Target model

FlashCpp should move toward a C++20 template implementation where:

- dependency classification is explicit
- non-dependent lookup remains definition-bound through instantiation
- dependent names preserve semantic identity end to end
- substitution, deduction, overload ranking, replay/materialization, and
  constexpr evaluation stay separate
- semantic analysis owns final call-target selection in normalized flows
- replay succeeds because invariant evidence matches, not because repair scans
  guessed a compatible declaration

## Current conformance baseline

The core suite now covers several formerly blocking standards-visible areas:

- definition-bound non-dependent lookup in covered template bodies
- semantic receiver-sensitive overload resolution for ordinary member calls,
  `operator[]`, and callable `operator()`
- viable direct-call conversion checks instead of parser-only optimistic
  struct-to-scalar matches
- structured direct-call metadata for covered ordinary, namespace-qualified,
  dependent-unqualified, qualified/member-template, operator function-id,
  postfix declaration-address, receiver-call, and function-pointer paths
- deferred qualified/member-template calls preserving explicit template
  arguments, dependent-qualified owner records, and parser return-type hints
- current-instantiation and nested-owner qualified lookup split consistently
  across parser, substitution, constexpr, and sema entry points via
  `ResolvedQualifiedOwner`
- identity-first replay/sync for the covered primary-template,
  specialization, nested-class, constructor, and member-template paths
- pack expansion preservation until substitution for unmatched complex call
  expansions
- member-function-pointer template arguments preserving declaring-class
  identity through substitution/materialization and MSVC mangling
- builtin type template arguments canonicalized for the MSVC `<type_traits>`
  `is_integral_v` fundamental character type fold

## Remaining standards gaps

### 1. Standard-header failures

The next conformance work should be driven by the remaining expected-failure
standard-header tests:

- `tests/std/test_cstddef.cpp`
- `tests/std/test_cstdio_puts.cpp`
- `tests/std/test_cstdlib.cpp`

Treat these as the active frontier. Run them individually, capture the first
failure, and trace the standard rule involved before editing. Likely failure
areas may include preprocessing/header modeling, builtin declarations, namespace
lookup, constexpr evaluation, or remaining template argument materialization
gaps. Do not preselect the layer.

### 2. Deeper dependent-qualified owner materialization

The direct owner-classification migration is complete for the identified parser
and consumer entry points. Further extraction is only justified by a concrete
failure showing that duplicate materialization still causes non-conforming
behavior.

If needed, the next abstraction should return a shared resolved owner and
member-prefix-chain result from a `DependentQualifiedNameRecord`, including:

- rebound owner-template arguments
- materialized member-template prefix segments
- current-instantiation vs unknown-specialization classification
- canonical owner type/name for later ordinary member lookup and explicit
  member-template lookup

### 3. Replay identity edge cases

Remaining replay work should be failure-driven. If a path still syncs
`StructTypeInfo` copies through weak name/arity evidence, replace that path
with source-member identity plus canonical substituted-signature matching.

### 4. Member-object-pointer representation

If another pointer-to-member ABI/conformance failure appears, preserve the
member object type explicitly in the canonical carrier rather than relying on
declarator-shaped pointer-depth/member-class metadata.

## Priority order

1. Fix the next concrete standard-header failure, starting with
   `test_cstddef.cpp`.
2. Add a focused non-std regression for the exposed rule when practical.
3. Only extract deeper dependent-qualified owner materialization if that failure
   proves the current consumer-specific prefix-chain handling is the blocker.
4. Keep replay identity and member-object-pointer work opportunistic unless a
   concrete regression points there.

## Standards rules for follow-up work

- do not normalize textual reconstruction as acceptable semantics
- prefer hard failure or correct diagnostics over silent repair in normalized
  semantic flows
- keep compatibility behavior explicit, narrow, and temporary
- treat codegen-side recovery as debt to remove, not architecture
- when a late retry is required, route it through typed semantic lookup or a
  structured record rather than string/shape reconstruction

## Validation guidance

For standard-conformance work, run:

- the focused regression or std-header test that motivated the slice
- adjacent template/lookup guards for the touched layer
- `.\build_flashcpp.bat`
- `pwsh ./tests/run_all_tests.ps1`

Common adjacent guards:

- `test_member_template_func_in_specialization_ret0.cpp`
- `test_template_qualified_owner_member_template_postfix_decltype_ret0.cpp`
- `test_sema_resolved_qualified_owner_template_nested_collision_ret0.cpp`
- `test_template_static_constexpr_qualified_nested_owner_collision_ret0.cpp`
- `test_var_template_replay_dependent_member_template_call_ret0.cpp`
- `test_template_disambiguation_pack_ret40.cpp`
- `std/test_std_type_traits.cpp`
