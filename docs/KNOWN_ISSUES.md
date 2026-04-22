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
that was resolved by preserving `cv_qualifier` through template type bindings
(see the entry below). Other partial-ordering gaps in non-SFINAE overload
selection are tracked separately.
**Standards note:** C++20 [temp.decls]/1 requires that a function template
defined after its declaration be treated as the same entity.

## SFINAE enable_if<false> not causing substitution failure in return type

**Status:** Resolved (2026-04-22). `test_namespaced_pair_swap_sfinae_ret0.cpp`
passes without the heuristic workaround.
**Root cause (historical):** `registerTemplateTypeBinding` dropped the
`cv_qualifier` on the bound `TemplateTypeArg` when registering the type alias,
so `is_const<First>` with `First=const int` silently saw plain `int` and the
`is_const<const T>` specialization failed to match. That made
`enable_if<!is_const<F>::value && !is_const<S>::value, void>::type` succeed
(predicate evaluated to `true` instead of `false`), so the `enable_if<false>`
return-type substitution failure that ought to have removed the non-deleted
overload never fired.
**Fix:** Both paths of `registerTemplateTypeBinding` now attach a
`TypeSpecifierNode` carrying the `cv_qualifier` when the bound argument is
cv-qualified. `parse_type_specifier` composes the alias-chain's `cv_qualifier`
with any explicit cv from the token stream at two existing resolution points
(direct `TypeInfo` lookup and the typedef/alias-chain path), so subsequent
pattern matching sees the concrete qualification.
**Tie-break narrowing:** With the cv-propagation fix in place, the former
"any best-specificity candidate is `= delete` ⇒ SFINAE failure" heuristic in
`try_instantiate_template` has been narrowed to its legitimate meaning:
SFINAE failure now requires **all** best-specificity candidates to be deleted.
A mix of deleted and non-deleted at the same specificity picks the non-deleted
one; the deleted overloads stay present as explicit sentinels for other input
shapes without short-circuiting resolution for the viable shape.

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

*(Fixed: see `tests/test_this_pointer_equality_ret0.cpp`. Binary-operator codegen
now checks `ExprResult::pointer_depth` before routing to the user-defined
operator lookup path, so pointer equality is always handled as a builtin
comparison per [expr.eq]/3.)*

