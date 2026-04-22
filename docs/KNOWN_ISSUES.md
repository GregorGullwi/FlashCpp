# Known Issues

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
**Residual issue:** None related to `enable_if<false>` substitution failure;
that was resolved by preserving `cv_qualifier` through template type bindings.
Other partial-ordering gaps in non-SFINAE overload selection are tracked separately.
**Standards note:** C++20 [temp.decls]/1 requires that a function template
defined after its declaration be treated as the same entity.

## Non-SFINAE function-template overload selection uses "first match" instead of most-specific

**Symptom:** In non-SFINAE call sites, `try_instantiate_template` returns the first
overload that instantiates successfully rather than the most-specialized one.  C++20
[temp.func.order] requires the most-specialized template to be preferred.
**Impact:** If multiple overloads are viable for a given call, the one declared first
wins regardless of specificity.  This silently produces wrong behavior rather than a
compile error, making it hard to detect.
**Minimal repro:**
```cpp
template <typename T> int f(T x) { (void)x; return 7; }
template <typename T> int f(T* x) { (void)x; return 42; }

int main() {
    int i = 0;
    return f(&i); // Should return 42 (T* more specific); currently returns 7.
}
```
**Root cause:** The non-SFINAE path in `try_instantiate_template` has a fast-return
after the first success.  The existing `hasLaterUsableTemplateDefinitionWithMatchingShape`
deferral only addresses the forward-declaration case, not the general partial-ordering
problem.
**Fix approach:** Apply the same `computeTemplateFunctionSpecificity` scoring used in
SFINAE selection to the non-SFINAE path as well.  Iterating in specificity-sorted order
(stable, ties preserve source order) and returning the first success gives
[temp.func.order] semantics without collecting all candidates — the first success in
sorted order is already the most-specialized viable overload.

**2026-04-22 attempt and findings:** The specificity-sorted iteration was implemented
and immediately fixes the minimal repro above (returns 42).  The single full-suite
regression it produces is `test_namespaced_pair_swap_sfinae_ret0.cpp`, and the root
cause is orthogonal to this issue:

 - That test exercises `is_swappable<pair<const int, int>>::value` via
   `decltype(swap(declval<pair&>(), declval<pair&>()))` in a default template argument.
   Under proper partial ordering, `swap(pair<F,S>&, pair<F,S>&)` (the pair-specialised
   overload) is the best match; in the immediate SFINAE context, the `= delete`
   pair-specialisation yields substitution failure per [temp.deduct.call] and
   `is_swappable` correctly becomes `false_type`.
 - The baseline passes the test "accidentally" because first-match selection picks
   the bodyless `swap(Type&, Type&)` forward declaration (with `Type = pair<const int, int>`)
   instead of the pair-specialised overload, which avoids ever materialising
   `pair<const int, int>::swap` and hides the ill-formed `swap(const int&, const int&)`
   call inside it.
 - With specificity-sorted iteration, the pair-specialised overload is correctly picked
   at one non-SFINAE call site; its body materialisation then pulls in
   `pair<const int, int>::swap`, whose body contains the ill-formed inner `swap` call,
   and codegen trips on the dependent-type that remains.
 - The non-SFINAE call in question should have been happening inside a SFINAE probe
   (it comes from a `decltype(...)` default-template-argument expression), but
   FlashCpp evaluates part of that expression in non-SFINAE context before the probe
   is wrapped.  This is precisely the symptom `InstantiationContext` threading
   (Slice G item #3) is designed to cure: a single `in_sfinae_context_` bit cannot
   distinguish "caller is in SFINAE" from "this particular sub-evaluation is not",
   and the leak is what causes the body to be eagerly materialised.

**Conclusion:** Attempting the fix confirmed the KNOWN_ISSUES description is correct
(the minimal repro is fixed immediately), and also confirmed the Slice G analysis
prognosis that items #2 and #3 must land together for principled SFINAE handling.
The KNOWN_ISSUES#2 fix is therefore **blocked on Slice G item #3**
(`InstantiationContext` threading).  Once item #3 makes
`decltype`-in-default-template-argument evaluations reliably SFINAE, the
specificity-sorted non-SFINAE path can land without regressing the `swap`/`is_swappable`
suite.

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



