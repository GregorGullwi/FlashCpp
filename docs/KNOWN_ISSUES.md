# Known Issues

## Alias-chain dependent-bool resolution loses size_bits (Phase 2)

**Test:** `test_alias_chain_dependent_bool_ret1.cpp`
**Symptom:** When a variable is declared via a dependent alias chain
(`require_integral<int>` → `enable_if_t<true, int>` → `enable_if<true, int>::type` → `int`),
the parser reports `size_bits=0` for the resulting variable.  Codegen warns:
`Parser returned size_bits=0 for identifier 'x' (type=23) - using fallback calculation`.
The IR allocates 0 bytes (`%x = alloc 0`).
**Impact:** The test still returns the correct value because the literal `1` is
propagated through a narrow return path.  However, the zero-size allocation
would cause incorrect behaviour if the variable were used in any expression
that depends on its stack size (e.g. address-of, array indexing, struct layout).
**Root cause:** The alias-template materialization path does not propagate
`size_bits` from the resolved underlying type back to the use-site
`TypeSpecifierNode` when the resolution goes through a dependent bool
non-type argument chain.
**Phase:** This is the exact kind of bug that Phase 2 alias-template
materialization consolidation is intended to fix.

## Function-template forward-declaration + definition instantiation picks wrong overload

**Status:** Partially resolved. The `hasLaterUsableTemplateDefinitionWithMatchingShape` check
now defers body-less matches in non-SFINAE context, redirecting to the later definition.

**Repro:**
```cpp
template <template <typename, int> class C, typename T, int N>
int useMixed(C<T, N>& c);                 // forward declaration

template <template <typename, int> class C, typename T, int N>
int useMixed(C<T, N>& c) {               // full definition
    const int& r = C<T, N>::size;
    return c.data + r;
}
```
**Symptom (resolved):** Link error: undefined reference to `useMixed(...)`.
The `hasLaterUsableTemplateDefinitionWithMatchingShape` check now defers body-less
matches in non-SFINAE context, so later full definitions are preferred.
**Residual issue:** The `enable_if<false>` substitution failure in complex SFINAE
scenarios (see below) can still result in incorrect overload selection when two
overloads have equal structural specificity but different enable_if conditions.
**Standards note:** C++20 [temp.decls]/1 requires that a function template
defined after its declaration be treated as the same entity.

## SFINAE enable_if<false> not causing substitution failure in return type

**Test:** `test_namespaced_pair_swap_sfinae_ret0.cpp` now passes with workaround.
**Symptom:** When a function template has `enable_if<false_condition>::type` as its
return type, FlashCpp may still consider the instantiation successful (as a bodyless
placeholder) rather than failing the substitution.
**Workaround applied:** In SFINAE overload selection, if any candidate at the
highest specificity level is `= delete`, the entire group is treated as a SFINAE
failure. This matches the intent of `= delete` overloads that explicitly catch
"error" cases (e.g., `swap` on `pair<const F, S>`).
**Proper fix needed:** The SFINAE instantiation path should detect `enable_if<false>`
return types and propagate them as substitution failures, eliminating the need for
the tie-breaking heuristic.
