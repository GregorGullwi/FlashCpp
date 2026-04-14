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

## Template-template parameter function deduction drops non-type (value) inner args

**Repro:**
```cpp
template <typename T, int N>
struct Array { T data; static constexpr int size = N; };

template <template <typename, int> class C, typename T, int N>
void use_mixed(C<T, N>& c) { (void)c; }

int main() {
	Array<int, 3> a;
	use_mixed(a);  // error: Failed to instantiate template function
}
```
**Symptom:** Compilation fails with `error: Failed to instantiate template function` /
`Non-type parameter not supported in deduction`.
**Impact:** Template functions whose template-template parameter has non-type
inner parameters cannot be called via argument deduction.  Struct-only usage
(e.g. `probe<Array>`) and explicit struct instantiation inside TTP bodies work
correctly.
**Root cause:** In `Parser_Templates_Inst_Deduction.cpp`, when a
`TemplateParameterKind::Template` parameter is deduced from a struct argument
the code only forwards type args (`!stored_arg.is_value`) into `deduced_type_args`
(lines 2013–2018).  Value args (`stored_arg.is_value`, e.g. the `N=3` in
`Array<int,3>`) are silently discarded.  When the deduction loop then reaches
the outer non-type parameter `N` it finds no entry in `param_name_to_arg` and
no deduced value to draw from, so it logs "Non-type parameter not supported in
deduction" and returns `std::nullopt`.
**Fix sketch:** Alongside `deduced_type_args` maintain a parallel
`deduced_value_args` list populated from `is_value` stored args, and consume
from it when the outer loop processes a `TemplateParameterKind::NonType`
parameter.

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



**Repro:**
```cpp
template <typename T>
struct identity {
	using type = T;
};

template <typename Prefix, template <typename> class Wrap, typename T>
struct holder {
	using value_type = typename Wrap<T>::type;
};

template <typename T>
concept has_ll_value_type = requires {
	requires sizeof(typename holder<char, identity, T>::value_type) == 8;
};
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
