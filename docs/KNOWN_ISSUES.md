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
**Symptom:** Link error: undefined reference to `useMixed(...)`.
When a function template is both forward-declared and later defined, both
declarations are pushed into `TemplateRegistry` as separate entries.  The
instantiation loop in `try_instantiate_single_template` finds the forward
declaration first (no body → `inline_always`), returns immediately, and
never reaches the full definition.
**Root cause:** `TemplateRegistry_Registry.h::registerTemplate` does not
replace a prior body-less entry when the full definition is registered,
unlike the equivalent class-template path which already merges forward
declarations.  An attempted fix (replacing forward-decl entries on
registration of a definition) caused a regression in SFINAE tests that use
`declval` — specifically `test_namespaced_pair_swap_sfinae_ret0.cpp` — because
the replacement matched `= delete` overloads with the same template/function
parameter count, altering the overload set in a way that broke lookup inside
SFINAE-gated default template arguments.
**Workaround:** The regression test `test_template_template_body_reparse_odr_use_ret0.cpp`
uses only the inline (non-forward-declared) definition form, which instantiates
correctly after the `isTemplateTemplateParameter` fix.
**Standards note:** C++20 [temp.decls]/1 requires that a function template
defined after its declaration be treated as the same entity.
