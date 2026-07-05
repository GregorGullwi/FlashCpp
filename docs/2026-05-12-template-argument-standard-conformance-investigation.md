# Template Argument Standard-Conformance Investigation

**Date:** 2026-05-12  
**Last updated:** 2026-07-05

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
- ordinary `inline` function metadata preserved through parser copies and IR,
  with COFF C-linkage inline definitions emitted as local/static symbols to
  avoid duplicate CRT definitions while preserving emitted C++ inline helpers
- function-template redeclaration default arguments are merged into later
  definitions, and covered function-template `noexcept(expr)` specifications
  are evaluated through the shared constexpr evaluator
- scoped-enum/std::byte shift and swap probes are covered, including an
  expected-fail guard for non-dependent builtin scoped-enum shifts
- MSVC `<tuple>` variable-template specialization patterns with nested
  template-ids and trailing packs are parsed through the shared explicit
  template-argument parser, and nested pack-pattern matching can bind a trailing
  pack against a multi-argument concrete template-id
- template-argument skipping preserves nested-template `>>` tokenization by
  splitting only when the skipped template-id consumes the inner `>` and leaves
  the caller-owned outer `>` available
- member class template partial-specialization parsing now rejects argument
  lists that are not more specialized than the primary parameter list
- namespace-qualified callable objects keep member-call semantics when the
  receiver is a concrete struct object, including the implicit object argument
  for instantiated constrained `operator()` member templates

## Remaining standards gaps

### 1. Standard-header tracking and next failure discovery

The formerly active expected-failure standard-header tests now compile, link,
and run on the current Windows baseline:

- `tests/std/test_cstddef.cpp`
- `tests/std/test_cstdio_puts.cpp`
- `tests/std/test_cstdlib.cpp`

The active standards-facing std-header failure is:

- `tests/std/test_std_ranges.cpp`

`test_cstdio_puts.cpp` exposed the relevant rule: C++ inline definitions in
headers must not be emitted as ordinary strong external definitions when a CRT
library also provides the C-linkage symbol. FlashCpp's current targeted behavior
is to retain and emit C++ inline header helpers when needed, while giving
C-linkage inline definitions local COFF symbol storage.

This is enough for the current Windows standard-header frontier, but it is not
the final C++20 linkage model. A later object-writer slice should replace the
local-symbol workaround with per-function COFF COMDAT/weak-external emission so
inline definitions deduplicate correctly across translation units.

The next standards-facing task is discovery: run the std subset again and let
the next concrete failure choose the layer. Likely future areas may include
preprocessing/header modeling, builtin declarations, namespace lookup,
constexpr evaluation, template argument materialization, or object/link
semantics. Do not preselect the layer.

For `std/test_std_ranges.cpp`, the constrained `std::byte` operator blocker is
now cleared. The instantiated `operator<<=` body can carry a parser-time
`NoMatch` for `byte << int`; sema now retries binary operator-template lookup
after concrete operand types are known, so the visible `operator<<` template is
selected instead of treating the expression as an invalid builtin scoped-enum
shift. The non-dependent invalid scoped-enum diagnostic remains covered by
`tests/test_scoped_enum_builtin_shift_fail.cpp`.

The previous MSVC `<tuple>:28` specialization-pattern failure is now covered by
`tests/test_variable_template_tuple_pack_pattern_ret0.cpp`.

The previous MSVC `<tuple>:138` allocator-aware constructor constraint is now
covered:

- `tests/test_template_nttp_unqualified_conjunction_alias_args_ret0.cpp`
- `tests/test_template_nttp_global_qualified_alias_args_ret0.cpp`

The covered shape is an unqualified `enable_if_t<conjunction_v<...>, int> = 0`
inside namespace `std`, where the operands are globally qualified traits such as
`::std:: uses_allocator<_Ty, _Alloc>`. The parser now keeps the
current-namespace `conjunction_v<...>` template-id on the value-like NTTP path
instead of treating it as a type-specifier.

The following MSVC `<tuple>` `_Equals` const-receiver failure is now covered by:

- `tests/test_template_const_member_function_template_receiver_ret0.cpp`

Member-function-template cv metadata now survives member-template registration,
class-template instantiation, and member-template materialization.

The previous standards-facing `std/test_std_ranges.cpp` `swap` overload and
dependent-placeholder materialization blocker is now covered. Member struct
template parameter kind metadata is preserved through partial-specialization
pattern parsing and dependent base parsing, and dependent member aliases remain
marked as dependent when used later as template arguments. The focused
regression is:

- `tests/test_member_struct_template_dependent_alias_template_arg_ret0.cpp`

The later `std/test_std_ranges.cpp` member-template parser blockers are now
covered:

- `tests/test_member_struct_template_ctor_brace_init_tail_ret0.cpp`
- `tests/test_member_struct_template_hidden_friend_operator_eq_ret0.cpp`

The namespace-qualified CPO receiver path is now covered by:

- `tests/test_constrained_cpo_call_operator_ret0.cpp`
- `tests/test_if_constexpr_static_enum_strategy_prune_ret0.cpp`

The active standards-facing `std/test_std_ranges.cpp` failure has moved to
semantic/template consistency:

- `function 'end' has inconsistent deduced auto return types`
- first visible conflict: `_Iterator<...>` versus `_Sentinel<...>` from
  `std::ranges::end`

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

1. Reduce and fix the current `std/test_std_ranges.cpp` inconsistent deduced
   `auto` return failure around `std::ranges::end`.
2. Keep the cleared dependent-alias and `std::byte` constrained-operator paths
   guarded with `tests/test_constrained_cpo_call_operator_ret0.cpp`,
   `tests/test_if_constexpr_static_enum_strategy_prune_ret0.cpp`,
   `tests/test_member_struct_template_dependent_alias_template_arg_ret0.cpp`,
   `tests/test_member_struct_template_ctor_brace_init_tail_ret0.cpp`,
   `tests/test_member_struct_template_hidden_friend_operator_eq_ret0.cpp`,
   `tests/test_member_struct_template_dependent_alias_const_assign_fail.cpp`,
   `tests/test_member_template_partial_specialization_same_args_fail.cpp`,
   `tests/test_member_template_partial_specialization_pack_echo_fail.cpp`,
   `tests/test_scoped_enum_shift_assign_operator_template_ret0.cpp`,
   `tests/test_mock_std_byte_ops_traits_ret0.cpp`, and
   `tests/test_scoped_enum_builtin_shift_fail.cpp`.
3. Remove stale expected-failure tracking for `test_cstddef.cpp`,
   `test_cstdio_puts.cpp`, and `test_cstdlib.cpp` where the harness still
   records it.
4. Only extract deeper dependent-qualified owner materialization if that failure
   proves the current consumer-specific prefix-chain handling is the blocker.
5. Keep replay identity and member-object-pointer work opportunistic unless a
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
- `test_mock_std_byte_noexcept_probe_ret0.cpp`
- `test_mock_std_byte_ops_traits_ret0.cpp`
- `test_scoped_enum_shift_assign_operator_template_ret0.cpp`
- `test_scoped_enum_builtin_shift_fail.cpp`
- `test_member_struct_template_dependent_alias_const_assign_fail.cpp`
- `test_member_template_partial_specialization_same_args_fail.cpp`
- `test_member_template_partial_specialization_pack_echo_fail.cpp`
- `std/test_std_type_traits.cpp`
- `std/test_std_ranges.cpp`
