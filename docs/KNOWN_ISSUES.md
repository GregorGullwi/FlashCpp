# Known Issues

## `#include <typeinfo>` fails to link — vtable back-substitution bug

**Symptom:** Compiling any TU that includes `<typeinfo>` produces an object file
that fails to link with undefined-reference errors:

```
undefined reference to `std::type_info::__do_catch(std::type_info const*, void**, unsigned int) const'
undefined reference to `std::type_info::__do_upcast(...) const'
```

**Root cause:** When FlashCpp parses `<typeinfo>` it sees the `std::type_info`
class definition and emits a local vtable for it (symbol `_ZTVSt9type_info`).
The virtual-function pointer slots in that vtable are filled by emitting
references to `_ZNKSt9type_info10__do_catchEPKSt9type_infoPPvj` and
`_ZNKSt9type_info11__do_upcastEPK10__cxxabiv117__class_type_infoPPv`.

However, libstdc++ exports these symbols with an Itanium ABI back-substitution:
`_ZNKSt9type_info10__do_catchEPKS_PPvj` (note `PKS_` = pointer-to-const-self,
back-substitution for `type_info`). The names do not match, so the linker
reports undefined references even though the symbols exist in the library.

**Impact:** Tests that use `typeid` must not include `<typeinfo>` and must
compare results via raw `const void*` pointer identity instead of
`std::type_info::operator==`. All RTTI regression tests in `tests/` follow
this pattern currently.

**Fix needed:** The name mangler must track substitutions and emit `S_` (or
the appropriate `S<n>_` back-reference) when a type that was already encoded
appears again in the same mangled name, per Itanium C++ ABI §5.1.8.

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

## Non-SFINAE function-template overload selection uses "first match" instead of most-specific

**Symptom:** In non-SFINAE call sites, `try_instantiate_template` returns the first
overload that instantiates successfully rather than the most-specialized one.  C++20
[temp.func.order] requires the most-specialized template to be preferred.
**Impact:** If multiple overloads are viable for a given call, the one declared first
wins regardless of specificity.  This silently produces wrong behavior rather than a
compile error, making it hard to detect.
**Root cause:** The non-SFINAE path in `try_instantiate_template` has a fast-return
after the first success.  The existing `hasLaterUsableTemplateDefinitionWithMatchingShape`
deferral only addresses the forward-declaration case, not the general partial-ordering
problem.
**Fix approach:** Apply the same `computeTemplateFunctionSpecificity` scoring used in
SFINAE selection to the non-SFINAE path as well, collecting all viable candidates before
selecting the best one.  The two paths would then share a single `selectBestCandidate`
helper, with SFINAE differing only in what happens when no candidate matches (silently
return `nullopt` vs. error).

## Unresolved-type detection relies on fragile heuristic (`UserDefined && size_in_bits() == 0`)

**Location:** `try_instantiate_single_template` codegen guard
(`src/Parser_Templates_Inst_Deduction.cpp`).
**Symptom:** Template instantiations whose parameter types still hold dependent
placeholders are blocked from codegen by checking `TypeCategory::UserDefined &&
size_in_bits() == 0`.  This heuristic can misfire on legitimate zero-size structs
(e.g., empty tag types, `std::monostate`) or miss placeholders stored under a
different category.
**Root cause:** There is no explicit `is_dependent` / `is_placeholder` flag on
`TypeSpecifierNode`; resolution status is inferred from size.
`DependentPlaceholderKind` already exists on `TypeInfo` but is not propagated
consistently to all `TypeSpecifierNode` use-sites.
**Fix approach:** Add a `bool is_dependent_` field to `TypeSpecifierNode` (or reuse
`DependentPlaceholderKind`) set at placeholder creation and cleared on resolution.
Replace the size-based guard with an explicit predicate `TypeSpecifierNode::is_dependent()`.

## MSVC standard headers can fail on `this` pointer comparisons in member functions

**Repro:** Including the full MSVC `<typeinfo>` header stack currently pulls in
`std::exception::operator=(const exception&)`, which contains:
```cpp
if (this == &_Other) {
	return *this;
}
```
**Symptom:** FlashCpp rejects this with `Operator== not defined for operand types`
while compiling the standard library, which blocks tests that rely on
`std::type_info` comparisons through `<typeinfo>`.
**Root cause:** In binary-operator codegen, pointer-to-struct expressions like
`this` still carry `TypeCategory::Struct`, and the struct comparison fallback path
ignores `pointer_depth`. That causes pointer equality to be misrouted through
user-defined/synthesized struct comparison handling instead of builtin pointer
comparison.
**Workaround:** Keep RTTI tests self-contained and compare the raw RTTI identity
returned by `typeid(...)` instead of pulling in the full MSVC `<typeinfo>` header
implementation.
  
## Implicit Derived*→PrivateBase* pointer conversion not rejected

**Repro:**
```cpp
struct Base { int x; };
struct Derived : private Base { int y; };
int main() {
    Derived d;
    Base* bp = &d;  // should be rejected per C++20 [conv.ptr]/3
    return bp->x;
}
```
**Symptom:** FlashCpp compiles this without error. A conforming compiler rejects
the conversion because `Base` is a private base of `Derived`.
**Root cause:** `isTransitivelyDerivedFrom` in `OverloadResolution.h` now correctly
filters non-public bases, but that function is only called during overload
resolution (function argument matching). The pointer variable initialization path
in `IrGenerator_Stmt_Decl.cpp` does not validate base accessibility — it silently
emits the derived-to-base pointer adjustment for any base class regardless of
access specifier.
**Fix approach:** Add an `isTransitivelyDerivedFrom` check (or equivalent) in the
IR generator's pointer initialization path. When the initializer is a struct
pointer with a different `TypeIndex` from the declaration, verify the base is
publicly accessible before emitting the adjustment; otherwise emit a compile error.

## Runtime aggregate initialization can still synthesize invalid default-ctor calls for nested non-aggregate members

**Repro:**
```cpp
struct Inner {
	int x;
	int y;
	constexpr Inner(int a, int b) : x(a), y(b) {}
};

struct Outer {
	Inner inner;
	int z;
};

int main() {
	Outer o = {{1, 2}, 3};
	return o.inner.x + o.inner.y + o.z;
}
```
**Symptom:** Link error: `undefined reference to 'Inner::Inner()'`.
**Impact:** Some runtime aggregate-initialization paths still materialize or emit an implicit default constructor for the enclosing aggregate even when a nested member must instead be initialized directly from the brace clause.
**Fix approach:** Reuse the constructor-aware nested brace-init lowering uniformly across the remaining runtime aggregate-initialization paths so nested non-aggregate members construct in-place instead of routing through a synthesized default constructor.
