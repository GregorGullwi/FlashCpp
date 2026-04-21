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
**Partial progress (item #2 data-model prerequisite, 2026-04-21):** The shape/body
phase split from `docs/2026-04-21-phase5-slice-g-analysis.md` item #2 is now in
place at the data-model level. `FunctionDeclarationNode` exposes a three-state
`BodyStateTag` (`NotMaterialized` / `Materialized` / `FailedSubstitution`) with
`is_materialized()`, `failed_substitution()`, `needs_body_materialization()`,
`has_any_body_source()`, and `mark_failed_substitution(reason)`. Category B
instantiation-driver sites on `FunctionDeclarationNode` now route through
`needs_body_materialization()` / `has_any_body_source()`, which short-circuit on
a `FailedSubstitution` node so a cached failure is never re-probed. Attempting to
remove the `= delete` tie-break heuristic while exercising this test confirmed
that the reparse-failure path in `try_instantiate_single_template` does not yet
catch every shape of `enable_if<false>` return type (specifically, `pair<First,
Second>&` forms where the outer template is already resolvable with placeholder
args). The heuristic therefore stays, but the infrastructure is now ready to
narrow it once every reparse-failure site calls `mark_failed_substitution` and
the reparse guard is tightened to cover the remaining shapes.
**Proper fix needed:** The SFINAE instantiation path should detect
`enable_if<false>` return types and propagate them as substitution failures via
the new `mark_failed_substitution` mutator, eliminating the need for the
tie-breaking heuristic.

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

