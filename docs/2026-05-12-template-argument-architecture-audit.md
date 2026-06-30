# Template Argument Architecture Audit

**Date:** 2026-05-12  
**Last updated:** 2026-06-30

This document is a planning aid for the remaining template-infrastructure work.
It is intentionally forward-looking: keep it focused on the current baseline,
the architectural invariants, and the next tasks. Avoid turning it into a
history of completed branches.

## Current architectural baseline

The template implementation has moved substantially toward sema-owned,
identity-preserving behavior:

- non-dependent template-body lookup remains definition-bound through covered
  replay/materialization paths
- dependent aliases and qualified owners preserve structured metadata instead
  of relying on textual reconstruction where covered
- direct-call resolution is increasingly represented by
  `FunctionCallDefinitionLookupRecord`,
  `DependentUnqualifiedCallLookupRecord`, or
  `DependentQualifiedNameRecord`, not by parser-selected callee fallbacks
- sema owns final overload selection for normalized direct/member calls,
  including const-aware receiver lookup, `operator[]`, callable `operator()`,
  and qualified direct calls in the covered paths
- replay and `StructTypeInfo` synchronization now prefer source-member
  identity plus canonical substituted-signature evidence over name/arity repair
- pack expansion in call arguments now preserves unmatched complex `expr...`
  nodes until substitution instead of flattening them early
- qualified/member-template parser paths now preserve explicit template
  arguments, parser return-type hints, dependent-qualified owner records, and
  definition-bound call metadata where concrete targets are known
- `ResolvedQualifiedOwner` is the shared owner-classification entry point for
  the parser, `ExpressionSubstitutor`, constexpr qualified member access, and
  sema qualified-call target recovery
- builtin type template arguments are canonicalized through
  `TemplateArgumentMaterialization.h`, covering the MSVC `<type_traits>`
  `is_integral_v` fundamental-character-type fold shape

## Architectural invariants

Follow-up work should preserve these rules:

- preserve semantic identity end to end; do not rebuild meaning from strings
  when a structured record exists
- keep parser-selected compatibility paths explicit, narrow, and temporary
- when sema has enough typed evidence, reject stale parser choices rather than
  silently recovering through compatibility metadata
- keep substitution, deduction, overload ranking, replay/materialization, and
  constexpr evaluation as separate responsibilities
- prefer identity-first replay attachment over `StructTypeInfo` repair scans
- add abstractions only when they remove real repeated owner/replay logic or
  prevent standards-visible fallback behavior from reappearing

## Remaining architectural gaps

### 1. Standard-header frontier

The green core suite is no longer blocked by the qualified-owner/direct-call
metadata work. The next high-value target is the remaining standard-header
expected-failure frontier:

- `tests/std/test_cstddef.cpp`
- `tests/std/test_cstdio_puts.cpp`
- `tests/std/test_cstdlib.cpp`

Start by running each focused test and identifying the first compiler failure.
Use the actual failure to decide whether the next slice belongs in
preprocessing, namespace/header modeling, template argument materialization,
constexpr evaluation, or semantic lookup. Do not assume the next failure is in
qualified-owner infrastructure.

### 2. Dependent-qualified owner prefix-chain extraction

Do not do this speculatively. The direct consumer migration to
`ResolvedQualifiedOwner` is complete for the currently identified parser,
substitution, constexpr, and sema entry points.

Extract deeper shared helpers only if a concrete failure proves that duplicate
handling of one of these is now the blocker:

- `DependentQualifiedNameRecord` owner-template argument rebinding
- member-prefix chain materialization
- current-instantiation vs unknown-specialization owner binding across
  substitution, constexpr, and sema

If extracted, the helper should consume the preserved dependent-qualified
record and return a resolved owner/prefix result that consumers can use without
re-splitting `qualified_name` or rebuilding owner identity from
`baseTemplateName()`.

### 3. Replay / `StructTypeInfo` sync debt

Most high-value replay identity gaps are closed in the covered template member
and nested constructor paths. Keep this bucket opportunistic unless a concrete
failure shows a remaining path syncing by weak name/arity evidence. When it
does, prefer routing the replay site through existing identity-first helpers
before adding new repair scans.

### 4. Member-object-pointer carrier

If a future ABI-sensitive pointer-to-member regression appears, close the
remaining canonical `TypeCategory::MemberObjectPointer` carrier gap by
preserving the underlying member type explicitly instead of relying on
declarator-shaped `member_class + pointer_depth` forms.

## Recommended next task

1. Start with the standard-header frontier:
   `test_cstddef.cpp`, `test_cstdio_puts.cpp`, then `test_cstdlib.cpp`.
2. For the first concrete failure, trace the real layer responsible before
   editing.
3. Add a focused regression that captures the standards-visible behavior outside
   the std-header test when practical.
4. Keep docs updates last and update this section with the next concrete
   frontier after validation.

## Validation guidance

For template-infrastructure changes, run the motivating focused test plus the
small guard set relevant to the touched layer. Common guards:

- `test_member_template_func_in_specialization_ret0.cpp`
- `test_template_qualified_owner_member_template_postfix_decltype_ret0.cpp`
- `test_template_qualified_owner_template_nested_decltype_collision_ret0.cpp`
- `test_sema_resolved_qualified_owner_template_nested_collision_ret0.cpp`
- `test_template_static_constexpr_qualified_nested_owner_collision_ret0.cpp`
- `test_var_template_replay_dependent_member_template_call_ret0.cpp`
- `test_template_qualified_member_template_nested_owner_collision_ret0.cpp`
- `test_template_qualified_member_template_nested_owner_chain_collision_ret0.cpp`
- `test_template_qualified_member_template_enclosing_owner_collision_ret0.cpp`
- `test_template_disambiguation_pack_ret40.cpp`
- `std/test_std_type_traits.cpp`

Before closing a slice, run:

- `.\build_flashcpp.bat`
- focused regressions for the changed behavior
- `pwsh ./tests/run_all_tests.ps1`
