# Template Argument Architecture Audit

**Date:** 2026-05-12  
**Last updated:** 2026-07-12

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
- Direct dependent alias template-ids remain structured until their enclosing
  arguments are known, then materialize before deduction and code generation.
- `ResolvedQualifiedOwner` and structured dependent-call records are the shared
  owner/lookup representation across parser, substitution, constexpr, and sema
  paths.
- Replay and instantiated-member synchronization use source identity and
  canonical substituted signatures instead of name/arity repair.

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

Fresh Windows/MSVC individual runner wall times after the 2026-07-12 rebuild:

| Test | Status | Time | Current frontier |
|------|--------|------|------------------|
| `std/test_cstddef.cpp` | Pass | 3.22s | Green control |
| `std/test_cstdio_puts.cpp` | Pass | 9.39s | Green control |
| `std/test_cstdlib.cpp` | Pass | 3.71s | Green control |
| `std/test_std_type_traits.cpp` | Pass | 5.07s | Green control |
| `std/test_std_iterator.cpp` | Fail | 22.97s | `ranges::empty` overload viability, then unresolved `auto` in `view_interface` mangling |
| `std/test_std_ranges.cpp` | Fail | 20.80s | `make_signed<T>` constraint/static-assert path and scoped-enum `_St` equality in tuple `_Equals` |

The full Windows suite passes: 2753 regular tests compile, link, and run; all
236 expected-failure tests fail as expected.

The current slice is guarded by:

- `tests/test_template_partial_specialization_inherited_static_call_pack_ret0.cpp`
- `tests/test_template_partial_specialization_reordered_identity_ret0.cpp`
- `tests/test_template_partial_specialization_array_bound_identity_ret0.cpp`
- `tests/test_template_dependent_inherited_static_call_pack_ret0.cpp`
- existing partial-specialization qualifier/pointer/array/function-pointer tests

## Remaining work

1. Reduce the variadic `std::invoke` trailing return/noexcept substitution
   failure around `_Invoker1<...>::_Call`. The one-argument candidate is
   correctly rejected for the two-argument call; the variadic candidate passes
   pack-aware viability, so this is a later substitution problem. Keep it
   separate from the completed empty-pack and partial-specialization fix.
2. Reduce the current `std/test_std_ranges.cpp` `make_signed<T>` and scoped-enum
   equality diagnostics. Determine whether the generic fault is constraint
   substitution, canonical enum identity, or overload selection before editing.
3. Reduce the `std/test_std_iterator.cpp` `ranges::empty` overload failure and
   unresolved-auto mangling path at the sema/return-type layer.
4. Re-isolate the independent `ranges::size` inconsistent-auto-return issue
   after earlier failures advance.
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
