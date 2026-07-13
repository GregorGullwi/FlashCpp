# Template Argument Standard-Conformance Investigation

**Date:** 2026-05-12  
**Last updated:** 2026-07-13

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

### Dependent placeholder return deduction

An `auto` return type in a dependent function body cannot be validated from a
mixture of concrete and still-dependent return expressions. Consistency waits
until substitution makes every participating type concrete. Comparison uses
canonical builtin/alias identity, but the original deduced node remains the
semantic result so validation does not perturb later lambda/member codegen.
Deferred `decltype` alias template-ids and braced constructions participate in
the same structured substitution path.

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

Static member functions remain members of their declaring class even though
they have no implicit object parameter. Their declaration and every
instantiated copy must therefore retain class-scope identity for unqualified
and nested-type lookup. Nested enum identity comes from the enum declaration,
not a same-spelled global registry entry.

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
`tests/test_template_partial_specialization_inherited_static_call_nonempty_pack_ret0.cpp`
guards trailing-return substitution with a non-empty pack, and
`tests/test_dependent_decltype_cpo_alias_auto_return_ret0.cpp` guards deferred
`decltype` alias materialization plus canonical `auto` return comparison.

## Live standard-header frontier

Windows/MSVC runner snapshot from 2026-07-13:

| Test | Status | Time | First captured issue |
|------|--------|------|----------------------|
| `test_std_cstddef.cpp` | Pass | 2.73s | — |
| `test_std_cstdio.cpp` | Pass | 2.76s | — |
| `test_std_cstdlib.cpp` | Pass | 2.62s | — |
| `test_std_type_traits.cpp` | Pass | 4.48s | — |
| `test_std_iterator.cpp` | Fail | 19.04s | `ranges::empty` viability, then missing `operator==` semantic annotation in `view_interface` |
| `test_std_ranges.cpp` | Fail | 32.40s | deferred `_Traits::compare`, `basic_string_view` `is_same_v` validation, and variadic `std::invoke` |

Full-suite result: 2758 regular tests pass and all 236 expected-failure tests
fail as expected.

## Active investigations

1. Variadic deferred-body replay

   The one-argument `std::invoke` overload is correctly rejected for a
   two-argument call. The variadic overload passes pack-aware count/shape
   viability with preserved `_Callable`, `_Ty1`, and `_Types2` bindings. The
   reduced non-empty-pack trailing-return/noexcept shape now passes, but the
   standard-header candidate still fails while replaying the inherited
   `_Invoker1<...>::_Call` body. Investigate replay lookup and materialized
   owner identity at the structured expression/type layer.

2. Iterator return-type frontier

   Reduce `ranges::empty` candidate viability and the missing `operator==`
   semantic annotation in `view_interface`. The unresolved-`auto` mangling
   diagnostic is gone; codegen must receive a resolved overload annotation.

3. Newly exposed ranges/string-view lookup

   The earlier `make_signed<T>`, scoped-enum, and inconsistent `ranges::size`
   auto-return diagnostics are removed. Reduce deferred `_Traits::compare`
   lookup and `basic_string_view` `is_same_v` validation separately from
   variadic call substitution.

4. Templated callable member-call codegen

   A reduced CPO-alias probe with a defined templated call operator exposed a
   later bug: `range.size()` can be emitted as an unresolved free `size`
   symbol. Preserve the member-call semantic binding through emission; do not
   add a spelling-based codegen fallback.

## Acceptance criteria

- The reduced valid C++20 program passes because generic deduction and
  substitution work, not because of spelling-based dispatch.
- Explicitly structured patterns continue to reject non-matching types.
- Invalid programs receive a semantic diagnostic rather than reaching codegen
  with unresolved types.
- Focused regressions, standard-header controls, and the full suite pass.
- The compiler-source diff contains no new `std::`, vendor helper, or
  compatibility-fallback logic.
