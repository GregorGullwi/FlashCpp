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
regression it produces is `test_namespaced_pair_swap_sfinae_ret0.cpp`.  The full
mechanistic trace (file-line precise) is recorded under
`docs/2026-04-21-phase5-slice-g-analysis.md` § 3 "2026-04-22 audit"; summary below.

 - The failing test parses `is_swappable<T>`'s
   `decltype(detail::test<T>(0))` base class, where `detail::test`'s first overload
   has a SFINAE default template argument
   `= decltype(swap(declval<T&>(), declval<T&>()))`.  FlashCpp eagerly evaluates
   that default-template-argument expression *at function-template-declaration
   time*, with `in_sfinae_context_ = false` and `T` still dependent.
 - Source-order first-match picks the 1-arg definition `swap(Type&, Type&)`
   whose deduced `T` remains a bare `TemplateParameterNode`.  Substitution logs
   `has_unresolved_params=true, registering=false` — the declaration-time parse
   is a no-op.
 - Specificity-sorted picks the pair-specialised `swap(pair<F,S>&, pair<F,S>&)`
   whose deduced args are the dependent *placeholder class-template instantiation*
   `pair$…` (a real `Struct` node).  Substitution logs
   `has_unresolved_params=false, registering=true`, the body
   `{ left.swap(right); }` is walked, and `registerLazyMember` at
   `src/Parser_Templates_Inst_ClassTemplate.cpp:6796` commits
   `pair$…::swap` to the lazy-member registry.  Later, when the real SFINAE
   probe instantiates `pair<const int, int>`, that registration fires and
   codegen trips on the ill-formed inner `swap(const int&, const int&)`.
 - The obvious narrow fix ("set `in_sfinae_context_ = true` around
   `parse_type_specifier()` in the three default-template-argument parse sites
   at `src/Parser_Templates_Params.cpp:162, 277, 362`") was also tried on
   2026-04-22.  It does *not* fix the pair/swap test — `in_sfinae_context_`
   controls only `try_instantiate_template`'s top-level iteration, not the
   substitution-time `registerLazyMember` call — and regresses
   `test_template_dependent_default_args_ret0.cpp` by turning legitimate
   dependent `typename decay_like<T>::type` defaults into silent failures.

**Conclusion:** Attempting the fix confirmed the KNOWN_ISSUES description is correct
(the minimal repro is fixed immediately), and pinpointed the exact side-effect that
blocks a narrow landing: `registerLazyMember` in
`src/Parser_Templates_Inst_ClassTemplate.cpp:6796` fires unconditionally during
substitution whenever the deduced arguments are concrete-looking, regardless of
the *intent* of the enclosing evaluation (declaration-time decltype probe vs.
real use site).  The KNOWN_ISSUES#2 fix is therefore **blocked on Slice G items
#2 and #3 landing together**:

 1. `InstantiationContext` (#3) must supply a `ShapeOnly` mode.
 2. That mode must propagate into the `registerLazyMember` site (#2, body/shape
    split for class templates).
 3. Default-template-argument parses at declaration time run with `ShapeOnly` →
    the pair-specialised overload still wins under specificity-sort, but its
    body is not materialised.

See `docs/2026-04-21-phase5-slice-g-analysis.md` § 3 "2026-04-22 audit" for the
concrete line-level spec of what `InstantiationContext` needs to do to unblock
this entry.

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



