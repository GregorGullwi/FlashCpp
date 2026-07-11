# Template Argument Standard-Conformance Investigation

**Date:** 2026-05-12  
**Last updated:** 2026-07-11

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
- direct dependent alias template-ids use shared classification, placeholder,
  and materialization infrastructure; substitution retains the complete type
  specifier so alias-introduced cv/ref/pointer structure survives every covered
  declaration, signature, expression, and class-member consumer

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
- `tests/test_qualified_cpo_nested_type_receiver_ret0.cpp`
- `tests/std/test_std_concepts_ranges_swap_int_ret0.cpp`

Deleted rvalue-reference function-template sentinels now preserve their deleted
status and participate in C++20 reference binding correctly: `const T&&` is not
a forwarding reference and cannot bind an lvalue argument.

- `tests/test_deleted_rvalue_template_overload_ret0.cpp`
- `tests/test_deleted_rvalue_template_overload_fail.cpp`

The `std::ranges::end` inconsistent auto-return path is now covered by:

- `tests/test_auto_return_if_constexpr_branch_prune_ret0.cpp`

The previous standards-facing `std/test_std_ranges.cpp` local proxy-class
failure is now covered:

- `tests/test_template_member_local_proxy_class_ret0.cpp`

The covered C++20 rule is that classes declared inside an instantiated member
function body are local classes, not nested member classes of the enclosing
class template. Their delayed bodies must not replay the enclosing member body,
and inheriting constructors declared by the local class remain usable for
brace-initialized local proxy objects.

The previous standards-facing `std/test_std_ranges.cpp` `std::char_traits`
instantiation loop is now covered:

- `tests/test_template_exact_specialization_reuse_ret0.cpp`

Exact class-template specializations participate in the same class-template
instantiation cache as primary-template materializations, so repeated
standard-header probes of a full specialization do not exhaust the global
instantiation-iteration budget.

The previous standards-facing `std/test_std_ranges.cpp` variable-template
declaration failure is now covered:

- `tests/test_variable_template_declaration_no_initializer_ret0.cpp`

C++14 variable templates can be declarations without an initializer; C++20
standard headers use this form for CPO-like objects such as
`std::views::empty`. Template declaration lookahead now recognizes the trailing
semicolon form before falling through to function-template parsing.

The previous standards-facing `std/test_std_ranges.cpp` constrained member
class-template partial-specialization failure is now covered:

- `tests/test_member_template_constrained_partial_spec_same_args_ret0.cpp`

The parser now accepts same-argument member class-template partial
specializations when either an explicit `requires` clause or constrained
template parameter makes the specialization more constrained than the primary.
The unconstrained same-argument rejection remains in place.

The previous standards-facing `std/test_std_ranges.cpp` template-depth failure
is now covered:

- `tests/test_member_template_self_specialization_param_no_eager_depth_ret0.cpp`

The live trace showed a valid finite chain of constrained class-template
viability checks, not a `std::remove_cv` semantic shortcut or recursive alias
bug. The parser depth guard remains finite but now has enough headroom for that
kind of conforming C++20 template chain.

Dependent alias-template type-ids and brace construction used by
`std::exchange`-like calls, including modifier-bearing direct aliases and
unused alias parameters, are now covered by:

- `tests/test_template_exchange_dependent_alias_default_ret0.cpp`
- `tests/test_dependent_direct_alias_modifiers_unused_ret0.cpp`

The reduced rule is generic: a direct alias template-id such as
`select_first_t<Left, FirstTail>` remains a structured instantiation until the
enclosing arguments are known, then materializes to its alias target before
function-template deduction and code generation. A concrete builtin target
must use its native type index, not an invalid category-only placeholder, while
the surrounding `TypeSpecifierNode` remains the carrier for declarator
modifiers that cannot be represented by `TypeIndex` alone.

The active standards-facing `std/test_std_ranges.cpp` failure has moved again:

- `[depth=1]: All 2 template overload(s) failed for 'std::invoke'`
- `function 'size' has inconsistent deduced auto return types: first return has
  type 'unknown', but another return has type 'std::ranges::_Size::_Cpo'`

Dependent qualified calls with concrete owners now retain structured lookup
identity while their call argument packs remain dependent. Known empty
function parameter packs expand to an empty argument list during
trailing-return SFINAE, and inherited static member-function templates are
deduced after substitution. The generic regression is:

- `tests/test_template_dependent_inherited_static_call_pack_ret0.cpp`

Direct function-parameter types now preserve their type-template-parameter
identity on `TypeSpecifierNode`, and shared deduction/materialization consumers
use `getStructuredTypeName(...)` instead of reconstructing identity from token
spelling. Fixed forwarding references before a trailing function parameter
pack use their exact parameter-to-argument mapping and the C++20 lvalue/rvalue
deduction adjustment. The generic regression is:

- `tests/test_function_template_fixed_param_before_pack_ret0.cpp`

The one-argument `std::invoke` overload is correctly rejected for a
two-argument call. Its variadic overload passes pack-aware count/shape
viability with structured `_Callable`, `_Ty1`, and `_Types2` identities, then
fails later during substitution of the `_Invoker1<...>::_Call` trailing
return/noexcept shape. The next slice should reduce that generic substitution
failure. Keep the `ranges::size` auto-return issue separate.

A reduced `<utility>` `std::addressof` probe now reaches a separate link
frontier: duplicate emitted definitions for std inline objects such as
`std::less`, `std::greater`, and `std::equivalent`. Keep that as an
object/linkage follow-up after the active `std::ranges::end` issue is merged or
otherwise no longer blocks `test_std_ranges.cpp`.

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

1. Reduce and fix the variadic `std::invoke` trailing return/noexcept
   substitution failure without hardcoding the standard-library name.
2. Reduce and fix the `ranges::size` CPO-object auto-return deduction issue
   without hardcoding standard-library names.
3. Keep the cleared auto-return, dependent-alias, alias-brace construction, and
   `std::byte`
   constrained-operator paths guarded with
   `tests/test_auto_return_if_constexpr_branch_prune_ret0.cpp`,
   `tests/test_template_exchange_dependent_alias_default_ret0.cpp`,
   `tests/test_deleted_rvalue_template_overload_ret0.cpp`,
   `tests/test_deleted_rvalue_template_overload_fail.cpp`,
   `tests/test_constrained_cpo_call_operator_ret0.cpp`,
   `tests/test_if_constexpr_static_enum_strategy_prune_ret0.cpp`,
   `tests/test_qualified_cpo_nested_type_receiver_ret0.cpp`,
   `tests/std/test_std_concepts_ranges_swap_int_ret0.cpp`,
   `tests/test_member_struct_template_dependent_alias_template_arg_ret0.cpp`,
   `tests/test_member_struct_template_ctor_brace_init_tail_ret0.cpp`,
   `tests/test_member_struct_template_hidden_friend_operator_eq_ret0.cpp`,
   `tests/test_member_struct_template_dependent_alias_const_assign_fail.cpp`,
   `tests/test_member_template_partial_specialization_same_args_fail.cpp`,
   `tests/test_member_template_partial_specialization_pack_echo_fail.cpp`,
   `tests/test_scoped_enum_shift_assign_operator_template_ret0.cpp`,
   `tests/test_mock_std_byte_ops_traits_ret0.cpp`, and
   `tests/test_scoped_enum_builtin_shift_fail.cpp`.
4. Remove stale expected-failure tracking for `test_cstddef.cpp`,
   `test_cstdio_puts.cpp`, and `test_cstdlib.cpp` where the harness still
   records it.
5. Only extract deeper dependent-qualified owner materialization if a concrete
   failure proves the current consumer-specific prefix-chain handling is the
   blocker.
6. Keep replay identity and member-object-pointer work opportunistic unless a
   concrete regression points there.

## Standards rules for follow-up work

- do not normalize textual reconstruction as acceptable semantics
- do not reduce a substituted alias target to base `TypeIndex` identity before
  preserving its complete declarator metadata
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
