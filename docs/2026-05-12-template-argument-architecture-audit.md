# Template Argument Architecture Audit

**Date:** 2026-05-12  
**Last updated:** 2026-07-13

This is a forward-looking planning reference for template infrastructure. Keep
it limited to the current architecture, invariants, live failures, and next
tasks; completed branch history belongs in tests and version control.

## Current architecture

- `TypeSpecifierNode`, `TemplateTypeArg`, and structured lookup records carry
  semantic identity through parsing, deduction, substitution, instantiation,
  replay, and sema. Consumers should not reconstruct it from token spelling or
  qualified names.
- Sema owns final overload selection once concrete types are available. Parser
  decisions are provisional and must not become compatibility fallbacks.
- Class-template partial-specialization matching distinguishes a bare dependent
  type parameter from an explicitly shaped pattern. Bare `T` deduces the whole
  argument, including cv/ref identity; `T&`, `const T`, `T*`, arrays, function
  signatures, and member pointers impose structural requirements.
- The selected specialization carries its named deduction map into
  materialization. Reordered, repeated, non-type array-bound, and
  template-template parameters are not reconstructed by position.
- Partial-specialization argument parsing uses the declared primary template's
  parameter kinds, so function types and template-template arguments are parsed
  into their semantic forms before matching.
- Member-function parameter substitution preserves both the complete type
  specifier and declaration metadata. In particular, a function-parameter pack
  remains a pack after class-template substitution, including when it expands
  to zero arguments.
- Dependent alias template-ids remain structured until their enclosing
  arguments are known, then materialize through the shared alias/substitution
  path before deduction and code generation, including deferred `decltype`
  targets.
- Placeholder-`auto` return analysis defers consistency checks while any return
  remains dependent. Once concrete, it compares canonical builtin/alias
  identities without replacing the original deduced node.
- Reparsed trailing return types pass through the same structured type
  substitution used by other instantiated declarations before member-alias
  resolution.
- `ResolvedQualifiedOwner` and structured dependent-call records are the shared
  owner/lookup representation across parser, substitution, constexpr, and sema
  paths.
- Replay and instantiated-member synchronization use source identity and
  canonical substituted signatures instead of name/arity repair.
- Class-scope ownership is independent of the implicit object parameter.
  Static member-function templates retain their qualified declaring scope
  through declaration copying so sema performs nested-type lookup in the
  correct class.
- Nested enum registration uses the declaration's `TypeIndex`; same-spelled
  enums are never recovered through an unqualified type-map lookup.

## Invariants

- Preserve semantic identity and declarator metadata end to end.
- Do not hardcode standard-library or vendor helper names in compiler logic.
- Do not introduce parser, sema, or codegen fallbacks for valid C++ constructs;
  implement the language rule at the first layer that owns it.
- Keep deduction, substitution, overload ranking, replay, constexpr evaluation,
  and code generation as separate responsibilities.
- A bare template parameter binds the complete concrete argument. Strip only
  modifiers explicitly contributed by the pattern.
- Partial-specialization deduction binds by semantic parameter identity, never
  by the parameter's position in the declaration.
- A copied declaration must preserve semantic flags such as parameter-pack,
  deleted/defaulted, cv/ref, and `noexcept` state as applicable to that copy.
- Do not replace a concrete substituted type with an unresolved placeholder or
  collapse a structured alias target to a base `TypeIndex` too early.
- Add a reduced non-`std` regression before fixing a standard-header failure.

## Current standard-header snapshot

Fresh Windows/MSVC individual runner wall times after the 2026-07-13 rebuild:

| Test | Status | Time | Current frontier |
|------|--------|------|------------------|
| `std/test_std_cstddef.cpp` | Pass | 2.73s | Green control |
| `std/test_std_cstdio.cpp` | Pass | 2.76s | Green control |
| `std/test_std_cstdlib.cpp` | Pass | 2.62s | Green control |
| `std/test_std_type_traits.cpp` | Pass | 4.48s | Green control |
| `std/test_std_iterator.cpp` | Fail | 19.04s | `ranges::empty` viability, then missing `operator==` semantic annotation in `view_interface`; unresolved-`auto` mangling is gone |
| `std/test_std_ranges.cpp` | Fail | 32.40s | deferred `_Traits::compare`, `basic_string_view` `is_same_v` validation, and variadic `std::invoke`; inconsistent `ranges::size` auto deduction is gone |

The full Windows suite passes: 2758 regular tests compile, link, and run; all
236 expected-failure tests fail as expected.

The current slice is guarded by:

- `tests/test_template_partial_specialization_inherited_static_call_pack_ret0.cpp`
- `tests/test_template_partial_specialization_inherited_static_call_nonempty_pack_ret0.cpp`
- `tests/test_dependent_decltype_cpo_alias_auto_return_ret0.cpp`
- `tests/test_template_partial_specialization_reordered_identity_ret0.cpp`
- `tests/test_template_partial_specialization_array_bound_identity_ret0.cpp`
- `tests/test_template_dependent_inherited_static_call_pack_ret0.cpp`
- `tests/test_member_template_nested_enum_identity_ret0.cpp`
- existing partial-specialization qualifier/pointer/array/function-pointer tests

## Remaining work

1. Reduce the variadic `std::invoke` deferred-body replay failure around
   `_Invoker1<...>::_Call`. Structured trailing-return substitution and the
   non-empty pack work in isolation, so inspect body replay and inherited
   static-call lookup rather than adding another signature fallback.
2. Reduce the `std/test_std_iterator.cpp` `ranges::empty` overload failure and
   missing `operator==` annotation at sema before the codegen visitor. The old
   unresolved-auto mangling path is complete.
3. Reduce the newly exposed `<ranges>` deferred `_Traits::compare` lookup and
   `basic_string_view` `is_same_v` validation independently of `std::invoke`.
4. Reduce the discovered templated-callable codegen path that emits
   `range.size()` as an unresolved free `size` symbol; preserve the resolved
   member-call metadata instead of repairing the name during emission.
5. Replace the targeted local-symbol treatment for C-linkage inline wrappers
   with proper per-function COFF COMDAT/weak-external emission when the object
   writer can split the monolithic text section.
6. Extract deeper dependent-qualified owner or replay helpers only when a
   concrete failure proves repeated consumer logic is the blocker.

## Validation

For each standards slice:

1. Add and run a reduced non-`std` regression.
2. Run adjacent guards for every semantic shape affected by the change.
3. Rebuild with `.\build_flashcpp.bat`.
4. Re-run the motivating standard-header file and green controls.
5. Run `pwsh ./tests/run_all_tests.ps1` before publication.
6. Inspect the compiler-source diff for standard-library/vendor spellings and
   run `git diff --check`.
