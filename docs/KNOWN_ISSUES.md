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

## Function-template body reparse loses ODR-used TTP value substitution

**Repro:**
```cpp
template <typename T, int N>
struct Array {
	T data;
	static constexpr int size = N;
};

template <template <typename, int> class C, typename T, int N>
int useMixed(C<T, N>& c) {
	return c.data + C<T, N>::size;
}

int main() {
	Array<int, 3> a{4};
	return useMixed(a) - 7;
}
```

**Symptom:** Parsing fails with:
`error: Expected type or expression after 'sizeof('`
at the end of the dependent `type-id`.
**Impact:** Valid C++20 nested requirements that use `sizeof(type-id)` on a
dependent qualified type are rejected, which forced the regression for
dependent-member `sizeof` constraints into a more indirect shape.
**Standards note:** `sizeof(type-id)` is valid in a nested requirement, and the
dependent `type-id` above is well-formed C++20 syntax.
