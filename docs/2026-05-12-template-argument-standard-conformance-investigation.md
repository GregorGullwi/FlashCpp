# Template Argument Standard-Conformance Investigation

**Date:** 2026-05-12  
**Last updated:** 2026-07-12

This document defines the standards-facing behavior still being pursued. It is
not a chronology of completed fixes.

## Semantic target

### Partial-specialization deduction

For a class-template partial specialization, a bare type parameter slot deduces
the complete corresponding template argument. Thus `Select<T, true>` can bind
`T` to `int&`, `const int`, an array, or another complete type. Pattern syntax
contributes only the structure it explicitly contains:

- `Box<T&>` requires an lvalue-reference argument and deduces the referred type.
- `Box<const T>` requires the matching cv qualification and removes only that
  contributed qualification from the deduction.
- Pointer, array, function, and member-pointer patterns follow the same rule.

Matching must use the structured pattern identity. A dependent template-id or
qualified dependent type is not interchangeable with a bare parameter merely
because it contains one. Reordered and repeated parameters bind by identity,
not by their declaration position.

### Function-parameter-pack substitution

A function-parameter pack is a property of its declaration, not an inference
from argument count or the substituted type. Every declaration-copy path used
by class/member-template substitution must retain that property. An empty pack
is a valid zero-argument expansion and must participate correctly in viability,
deduction, trailing return types, `noexcept`, and body substitution.

### Layer ownership

- Parsing records template parameters, declarator structure, and pack metadata.
- Deduction matches structured patterns and produces complete bindings.
- Substitution creates declarations that preserve source semantics while
  replacing dependent identities.
- Specialization materialization consumes the matcher's named bindings; it
  never infers a binding from declaration or argument position.
- Sema performs final lookup, overload viability, and return-type analysis.
- Code generation consumes resolved semantics and must not repair them.

No layer may recognize standard-library or vendor helper names to make a header
compile.

## Covered behavior

`tests/test_template_partial_specialization_inherited_static_call_pack_ret0.cpp`
combines the two rules above:

- forwarding-reference arguments instantiate a class template with reference
  template arguments;
- a partial specialization with bare parameters must match those complete
  reference arguments;
- an inherited static member-function template retains its trailing pack after
  class-template substitution;
- an empty pack is valid in the call, trailing return type, and `noexcept`.

The neighboring
`tests/test_template_dependent_inherited_static_call_pack_ret0.cpp` and existing
partial-specialization cv/ref/pointer/array/function-pointer tests guard the
adjacent semantics. `tests/test_template_partial_specialization_reordered_identity_ret0.cpp`
guards reordered parameter identity after positional matcher recovery was
removed, and `tests/test_template_partial_specialization_array_bound_identity_ret0.cpp`
verifies that a direct `T[N]` bound is deduced into the specialization body.

## Live standard-header frontier

Windows/MSVC runner snapshot from 2026-07-12:

| Test | Status | Time | First captured issue |
|------|--------|------|----------------------|
| `test_cstddef.cpp` | Pass | 3.22s | — |
| `test_cstdio_puts.cpp` | Pass | 9.39s | — |
| `test_cstdlib.cpp` | Pass | 3.71s | — |
| `test_std_type_traits.cpp` | Pass | 5.07s | — |
| `test_std_iterator.cpp` | Fail | 22.97s | `ranges::empty` overload viability, then unresolved `auto` in `view_interface` mangling |
| `test_std_ranges.cpp` | Fail | 20.80s | `make_signed<T>` constraint/static-assert path and scoped-enum `_St` equality in tuple `_Equals` |

Full-suite result: 2753 regular tests pass and all 236 expected-failure tests
fail as expected.

## Active investigations

1. Variadic trailing-return substitution

   The one-argument `std::invoke` overload is correctly rejected for a
   two-argument call. The variadic overload passes pack-aware count/shape
   viability with preserved `_Callable`, `_Ty1`, and `_Types2` bindings, then
   fails while substituting `_Invoker1<...>::_Call` in its trailing
   return/noexcept shape. Reduce this with a non-`std` non-empty-pack test and
   fix substitution at the structured expression/type layer.

2. Current ranges failure

   Trace the `make_signed<T>` constraint/static-assert and tuple `_Equals`
   scoped-enum comparison independently. Establish whether the enum operands
   should have one canonical type, whether a constraint should have removed the
   candidate, or whether overload lookup selected the wrong operation.

3. Iterator return-type frontier

   Reduce `ranges::empty` candidate viability separately from the unresolved
   `auto` that reaches `view_interface` mangling. Resolved return types must be
   established before code generation.

4. Independent `ranges::size` issue

   Reconfirm the inconsistent-auto-return diagnostic after the earlier ranges
   failures are removed. Keep it separate from variadic call substitution.

## Acceptance criteria

- The reduced valid C++20 program passes because generic deduction and
  substitution work, not because of spelling-based dispatch.
- Explicitly structured patterns continue to reject non-matching types.
- Invalid programs receive a semantic diagnostic rather than reaching codegen
  with unresolved types.
- Focused regressions, standard-header controls, and the full suite pass.
- The compiler-source diff contains no new `std::`, vendor helper, or
  compatibility-fallback logic.
