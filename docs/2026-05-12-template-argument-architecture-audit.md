# Template Argument Architecture Audit

**Date:** 2026-05-12  
**Last updated:** 2026-07-11

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
- ordinary function declarations now preserve the C++ `inline` specifier through
  parser copies and IR metadata; COFF emission gives C-linkage inline
  definitions local/static symbol storage so UCRT wrapper definitions do not
  collide with CRT library symbols while C++ inline header helpers remain
  emitted
- function-template redeclarations merge default template arguments into later
  definitions, and function-template `noexcept(expr)` metadata is preserved
  through the shared constexpr evaluator for the covered std-header probes
- parser trailing-specifier handling now routes cv/ref qualifiers through
  `parse_cv_qualifiers()` and `parse_reference_qualifier()` in the common
  function-header, skip, and template-function paths
- variable-template partial-specialization patterns now reuse the explicit
  template-argument parser, so nested template-ids and pack patterns such as
  `tuple<Dests...>` are preserved for matching; nested trailing packs can bind
  against multi-argument concrete template-ids
- template-argument skipping preserves C++ maximal-munch tokenization by
  splitting `>>` only when a skipped template-id closes at depth one and the
  caller still owns the outer `>`
- member class template partial specializations reject argument lists that
  exactly echo the primary template parameter list; the remaining positive
  member-specialization guards use standards-valid discriminator patterns
- namespace-qualified callable-object calls such as `ranges::swap(...)` keep
  the member-call path when the qualified receiver is a concrete struct object,
  so instantiated `operator()` member templates receive the implicit object
  argument and preserve reference-argument mutation
- direct dependent alias template-ids use one classifier, structured placeholder
  factory, and materializer across parsing and substitution; full
  `TypeSpecifierNode` metadata carries alias-introduced cv/ref/pointer structure
  through signatures, bodies, and class-template member layout

## Architectural invariants

Follow-up work should preserve these rules:

- preserve semantic identity end to end; do not rebuild meaning from strings
  when a structured record exists
- keep parser-selected compatibility paths explicit, narrow, and temporary
- when sema has enough typed evidence, reject stale parser choices rather than
  silently recovering through compatibility metadata
- keep substitution, deduction, overload ranking, replay/materialization, and
  constexpr evaluation as separate responsibilities
- do not replace a concrete substituted type with an unresolved placeholder
  index, and do not use a category-only builtin type where a native `TypeIndex`
  is available
- do not collapse a substituted alias target to `TypeIndex` before consumers
  have retained its cv/ref/pointer/array/function declarator metadata
- prefer identity-first replay attachment over `StructTypeInfo` repair scans
- add abstractions only when they remove real repeated owner/replay logic or
  prevent standards-visible fallback behavior from reappearing

## Remaining architectural gaps

### 1. Standard-header tracking cleanup

The previously listed Windows std-header frontier is green on the current
baseline:

- `tests/std/test_cstddef.cpp`
- `tests/std/test_cstdio_puts.cpp`
- `tests/std/test_cstdlib.cpp`

The active std-header frontier is now:

- `tests/std/test_std_ranges.cpp`

The concrete blocker was `<cstdio>` link failure from strong definitions of
MSVC UCRT C-linkage inline wrappers such as `vsnprintf`, `snprintf`, and
`sprintf_s`. The parser now preserves ordinary `inline` metadata and codegen
emits C-linkage inline definitions as local COFF symbols rather than public
external definitions. Keep C++ inline header helpers emitted;
`std/test_std_type_traits.cpp` depends on that for helpers such as
`std::_Fnv1a_append_bytes`.

This is a targeted COFF fix, not complete inline linkage architecture. Proper
cross-translation-unit COFF inline semantics should move inline definitions into
deduplicatable per-function COMDAT/weak-external sections once the object writer
can split the current monolithic `.text` section by function.

The next high-value cleanup is to use the next live standard-header failure as
the driver. Do not assume it is in template-owner infrastructure; trace whether
it belongs in preprocessing, namespace/header modeling, template argument
materialization, constexpr evaluation, semantic lookup, or object/linkage
emission.

For `std/test_std_ranges.cpp`, the previous dependent overload path is now
cleared: the stale parser `NoMatch` for constrained `std::byte` `operator<<` is
repaired during sema once the instantiated `operator<<=` body has concrete
`byte` and `int` operand types. The scoped-enum diagnostic remains guarded for
invalid non-dependent builtin shifts, and
`tests/test_scoped_enum_shift_assign_operator_template_ret0.cpp` covers the
late operator-template retry.

The previous `<tuple>:28` variable-template specialization-pattern blocker is
covered by `tests/test_variable_template_tuple_pack_pattern_ret0.cpp`.

The previous MSVC `<tuple>:138` allocator-aware constructor constraint is now
covered:

- `tests/test_template_nttp_unqualified_conjunction_alias_args_ret0.cpp`
- `tests/test_template_nttp_global_qualified_alias_args_ret0.cpp`

The covered shape is an unqualified alias-template NTTP type in namespace `std`:
`enable_if_t<conjunction_v<::std:: uses_allocator<...>, ::std:: is_constructible<...>>, int> = 0`.
The parser now recognizes current-namespace variable-template-ids as value-like
arguments before falling through to type parsing.

The subsequent MSVC `<tuple>` `_Equals` const-receiver failure is also covered:

- `tests/test_template_const_member_function_template_receiver_ret0.cpp`

Member-function-template cv metadata is now preserved from parsing through
class-template instantiation and member-template materialization.

The previous `std/test_std_ranges.cpp` `swap` overload and dependent-placeholder
classification blocker is now covered by preserving full member-struct-template
parameter metadata while parsing partial-specialization patterns and dependent
bases, and by keeping dependent member aliases marked as dependent when they are
later used as template arguments. The focused regression is:

- `tests/test_member_struct_template_dependent_alias_template_arg_ret0.cpp`

The previous `std/test_std_ranges.cpp` member-template parser blockers are now
covered:

- `tests/test_member_struct_template_ctor_brace_init_tail_ret0.cpp`
- `tests/test_member_struct_template_hidden_friend_operator_eq_ret0.cpp`

These guard the `transform_view::_Iterator` shapes where a skipped member class
template constructor ends with a braced member initializer before the function
body, and where an inline hidden-friend `operator==` appears inside the member
class template body.

The namespace-qualified callable-object receiver path is now covered by:

- `tests/test_constrained_cpo_call_operator_ret0.cpp`
- `tests/test_if_constexpr_static_enum_strategy_prune_ret0.cpp`
- `tests/test_qualified_cpo_nested_type_receiver_ret0.cpp`
- `tests/std/test_std_concepts_ranges_swap_int_ret0.cpp`

Deleted rvalue-reference function-template sentinels now preserve their
`= delete` metadata and no longer accept lvalue arguments through accidental
forwarding-reference treatment:

- `tests/test_deleted_rvalue_template_overload_ret0.cpp`
- `tests/test_deleted_rvalue_template_overload_fail.cpp`

The `std::ranges::end` inconsistent auto-return frontier is now covered by:

- `tests/test_auto_return_if_constexpr_branch_prune_ret0.cpp`

The previous `std/test_std_ranges.cpp` `common_iterator` local proxy-class
frontier is now covered:

- `tests/test_template_member_local_proxy_class_ret0.cpp`

Local classes parsed inside instantiated template member bodies now keep their
own delayed function-body queue instead of replaying the enclosing member body
recursively. They are also marked as local classes so template class
instantiation does not copy them as owner member classes. Inheriting
constructors declared by local classes, such as `using _Proxy_base::_Proxy_base`,
are materialized during instantiated member-body parsing so brace-initialized
proxy objects can select the inherited base constructor.

The previous `std/test_std_ranges.cpp` `std::char_traits` instantiation loop is
now covered:

- `tests/test_template_exact_specialization_reuse_ret0.cpp`

Exact class-template specializations now register in the class-template
instantiation cache after materialization, including the unqualified cache key
used by namespace-qualified templates. Repeated `std::char_traits<char>` probes
therefore hit the existing early cache instead of consuming the global
instantiation-iteration budget.

The previous `std/test_std_ranges.cpp` variable-template declaration blocker is
now covered:

- `tests/test_variable_template_declaration_no_initializer_ret0.cpp`

Template declaration lookahead now recognizes declaration-only variable
templates such as `template <class T> constexpr empty_view<T> empty;` as
variable templates instead of falling through to function-template parsing.

The previous `std/test_std_ranges.cpp` constrained member class-template
partial-specialization blocker is now covered:

- `tests/test_member_template_constrained_partial_spec_same_args_ret0.cpp`

The member class-template partial-specialization guard now treats constrained
template parameters as a specialization discriminator, matching the existing
explicit `requires` handling while preserving the rejection of unconstrained
argument lists that merely echo the primary template parameter list.

The previous `std/test_std_ranges.cpp` template-depth blocker is now covered:

- `tests/test_member_template_self_specialization_param_no_eager_depth_ret0.cpp`

`std::remove_cv` was not the root cause; it appeared inside a valid finite
chain of constrained class-template viability checks. The parser nesting guard
now has enough headroom for those conforming chains while still retaining a
finite runaway-recursion diagnostic.

Dependent alias-template type-ids and brace construction used by
`std::exchange`-like calls, including modifier-bearing direct aliases and
unused alias parameters, are now covered by:

- `tests/test_template_exchange_dependent_alias_default_ret0.cpp`
- `tests/test_dependent_direct_alias_modifiers_unused_ret0.cpp`

This preserves direct aliases such as `select_first_t<Left, FirstTail>` through
member-function signature materialization, materializes the target after the
enclosing class-template argument is known, and lets ordinary
function-template deduction consume the concrete argument type on both Windows
and Linux. Class-template member layout now consumes the full substituted type
specifier, so modifiers introduced by a member alias are not lost when its base
identity is canonicalized to a `TypeIndex`.

The active `std/test_std_ranges.cpp` frontier has moved again:

- `[depth=1]: All 2 template overload(s) failed for 'std::invoke'`
- `function 'size' has inconsistent deduced auto return types: first return has
  type 'unknown', but another return has type 'std::ranges::_Size::_Cpo'`

Reduce the next slice without hardcoding standard-library names. Start from the
generic overload-resolution/constraints path used by `invoke`, and separately
reduce the CPO-object auto-return deduction issue behind `ranges::size`. Keep a
non-`std` regression for each accepted language rule.

A standalone `<utility>` `std::addressof` probe now gets past the deleted
`const T&&` overload selection issue and exposes duplicate emitted std inline
objects such as `std::less`, `std::greater`, and `std::equivalent`. Treat that
as the next object/linkage std-header reduction after the active
`std::ranges::end` semantic frontier is cleared or merged forward.

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

1. Reduce the current `std/test_std_ranges.cpp` `std::char_traits`
   instantiation loop into a focused cache/reuse or recursion guard test.
2. Trace whether repeated `char_traits` materialization is caused by missing
   class-template instantiation reuse, a stale failed-instantiation state,
   specialization lookup drift, or a replay side effect before changing the
   iteration limit.
3. Keep `tests/test_auto_return_if_constexpr_branch_prune_ret0.cpp`,
   `tests/test_deleted_rvalue_template_overload_ret0.cpp`,
   `tests/test_deleted_rvalue_template_overload_fail.cpp`,
   `tests/test_constrained_cpo_call_operator_ret0.cpp`,
   `tests/test_if_constexpr_static_enum_strategy_prune_ret0.cpp`,
   `tests/test_qualified_cpo_nested_type_receiver_ret0.cpp`,
   `tests/std/test_std_concepts_ranges_swap_int_ret0.cpp`,
   `tests/test_member_struct_template_dependent_alias_template_arg_ret0.cpp`,
   `tests/test_member_struct_template_ctor_brace_init_tail_ret0.cpp`,
   `tests/test_member_struct_template_hidden_friend_operator_eq_ret0.cpp`,
   `tests/test_scoped_enum_shift_assign_operator_template_ret0.cpp`,
   `tests/test_mock_std_byte_ops_traits_ret0.cpp`, and
   `tests/test_scoped_enum_builtin_shift_fail.cpp` in the guard set so the
   cleared `std::byte` operator and dependent-alias paths do not regress.
4. Re-run `std/test_std_ranges.cpp` after the semantic/template fix and let the
   next concrete failure choose the following layer.
5. Promote stale expected-failure tracking only after the corresponding harness
   entry is proven green.

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
- `test_mock_std_byte_noexcept_probe_ret0.cpp`
- `test_mock_std_byte_ops_traits_ret0.cpp`
- `test_scoped_enum_shift_assign_operator_template_ret0.cpp`
- `test_scoped_enum_builtin_shift_fail.cpp`
- `test_member_struct_template_dependent_alias_const_assign_fail.cpp`
- `test_member_template_partial_specialization_same_args_fail.cpp`
- `test_member_template_partial_specialization_pack_echo_fail.cpp`
- `std/test_std_type_traits.cpp`
- `std/test_std_ranges.cpp`

Before closing a slice, run:

- `.\build_flashcpp.bat`
- focused regressions for the changed behavior
- `pwsh ./tests/run_all_tests.ps1`
