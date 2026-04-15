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

## Static constexpr member initializer fails when accessing a member via TTP instantiation

**Repro:**
```cpp
template <typename T>
struct box { static constexpr int id = sizeof(T); };

template <template <typename> class W>
struct probe {
    static constexpr int sz = W<int>::id;  // error: Failed to parse initializer expression
};

int main() { return probe<box>::sz - 4; }
```
**Symptom:** Parsing fails with `error: Failed to parse initializer expression`
when a `static constexpr` member initializer in a template struct directly
instantiates a template-template parameter with explicit arguments and then
accesses a member via `::`.
**Impact:** Accessing static members (constants, type aliases) of an on-the-spot
TTP instantiation (`W<int>::id`) in a `static constexpr` initializer is
rejected.  Workaround: introduce an intermediate `using` alias
(`using inner = W<int>;`) and reference that instead (`inner::id`), which
parses and compiles correctly.
**Root cause:** The parser's static-member initializer expression path does not
recognise `W<Args>` as a valid primary expression when `W` is a template-template
parameter, so it fails before it can resolve the `::` scope qualifier.

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

## Inherited member access inside inherited dereference operators can crash at runtime

**Repro:**
```cpp
struct DerefBase {
	int* ptr;
	int& operator*() { return *ptr; }
};

struct Iter : DerefBase {
	Iter& operator++() { ++ptr; return *this; }
	bool operator!=(Iter other) const { return ptr != other.ptr; }
};

int main() {
	int values[1] = {42};
	Iter it;
	it.ptr = values;
	int x = *it;
	return x - 42;
}
```
**Symptom:** Compilation succeeds, but the generated program segfaults when an
inherited `operator*` implementation dereferences a base-class data member.
**Impact:** Iterator-like types that inherit their dereference operator from a
base class are not safe when that operator body reads inherited state, even
though simpler inherited `operator*` bodies (for example, returning a constant)
work.
**Root cause:** Still under investigation. The failure appears in the
member-function/codegen path for inherited dereference operators rather than in
the new sema pre-resolution step.
