# Template Instantiation / Materialization Status

**Date:** 2026-04-08  
**Last Updated:** 2026-04-23 (Phase 6 nested wrapped pack deduction/materialization)

This document is now a short status audit, not a historical scratchpad.
Its purpose is to answer two questions clearly:

1. What is already done?
2. What are the next concrete steps?

## Current status

- **Phase 0:** Done. The original regression cluster remains the guardrail.
- **Phase 1:** Done. Non-type template-argument identity now has one canonical key path.
- **Phase 2:** Done. Alias-template materialization is centralized through shared helpers.
- **Phase 3:** Done. Late-materialized roots now have an explicit register/normalize lifecycle.
- **Phase 4:** Done. Dependent placeholder state is explicit via `DependentPlaceholderKind`.
- **Phase 5:** Done.
  - The materialization-ownership cleanup is complete. Sema owns lazy-member materialization, the ODR-use drain is in place, and codegen no longer acts as a first materializer.
  - The long Phase 5 slice chain (A through M) is effectively closed by the current tree state captured below in the validation history.
- **Phase 6:** In progress.
  - The 2026-04-22 positional-fallback bugfix landed: fallback is gated on an actual **function-parameter pack** and skips pre-deduced call-arg slots. Regression: `tests/test_explicit_variadic_pack_nondeduced_tail_fail.cpp`.
  - **2026-04-23 fold Pattern 3 bare-id backtracking fix landed:** the binary-left-fold parser Pattern 3 `(init op ... op <pack>)` path consumed an identifier before checking `)`, but did not restore the token position on failure. This made `(0 + ... + args.x)` (and any member-access or complex expression starting with an identifier) fall through as a non-fold parenthesized expression. Fix: save token position before consuming the identifier and restore on `)`-mismatch so the complex-expression fallback can re-parse the full expression. Regression: `tests/test_fold_member_access_ret42.cpp`.
  - **2026-04-23 pack expansion in brace initializer lists landed:** `{args...}` and `{static_cast<int>(args)...}` inside brace-init lists are now parsed as `PackExpansionExprNode` elements. Two parser sites updated (`parse_brace_initializer_clause_list` in `src/Parser_Statements.cpp` and the array-element branch of `parse_brace_initializer`). `substituteTemplateParameters` for `InitializerListNode` now routes non-designated `PackExpansionExprNode` elements through the existing `expandPackExpansionArgs` helper so each pack element becomes a separate initializer in the substituted list. Regression: `tests/test_pack_expansion_brace_init_ret42.cpp` (explicit-size array form). Known-issue corollaries (documented in `docs/KNOWN_ISSUES.md`): unsized `int arr[] = {args...}` keeps pre-expansion size 1 because `deduceArraySize` runs at parse time; aggregate struct brace-init with a pack-expansion element is still rejected at parse time on a different code path.
  - The explicit-deduction cleanup slice landed: explicit function-template instantiation now reuses the canonical function-parameter → call-argument mapping for pack slices, including **mixed explicit + deduced pack** cases. Regressions: `tests/test_explicit_variadic_pack_deduction_ret0.cpp` and `tests/test_explicit_variadic_pack_trailing_default_ret0.cpp`.
  - **2026-04-23 multi-pack slice landed:** Two bugs fixed for templates that have multiple variadic packs (e.g. `template<int... Ns, typename... Ts>`):
    1. `try_instantiate_template_explicit`: the call-arg-slice deduction check is now gated on the template-parameter pack that actually maps to the function-parameter pack (via the new `CallArgDeductionInfo::function_pack_template_param_name` field), preventing a false `overload_mismatch` for non-function packs.
    2. `substituteTemplateParameters` / `SizeofPackNode` handler: the accurate per-pack element count is now propagated from `try_instantiate_template_explicit` through the new `template_param_pack_sizes_` member, so `sizeof...(Ns)` and `sizeof...(Ts)` resolve to the correct individual counts rather than the overcounted `template_args.size() - non_variadic_count`. Regressions: `tests/test_explicit_type_pack_not_in_func_sig_ret0.cpp`, `tests/test_explicit_nontype_pack_not_in_func_sig_ret0.cpp`, `tests/test_multi_pack_sizeof_deduction_ret0.cpp`.
  - **2026-04-23 fold-expression complex-pack parser fix landed:** Two parser patterns extended to handle non-bare-identifier pack expressions per the C++17/20 standard (`[expr.prim.fold]`):
    1. **Pattern 1** `(... op cast-expr)`: the unary-left-fold handler now tries to parse a full cast expression when the token after `... op` is not a bare identifier. Previously any expression like `(args > 0)` was silently rejected and caused a template instantiation failure for common patterns such as `(... && (args > 0))`.
    2. **Pattern 3** `(init op ... op cast-expr)`: the binary-left-fold handler received the same extension for the right-hand operand, enabling `(0 + ... + (args * 2))`.
    A new `FoldExpressionNode` constructor was added for the binary-fold + complex-pack case. Regression: `tests/test_fold_expr_complex_pack_ret0.cpp`.
  - **2026-04-23 implicit pack deduction from template-specialisation parameters landed:**
    When a function-parameter pack has a template-specialisation element type (e.g. `Box<Ts>...`), `deduceTemplateArgsFromCall` now extracts the inner type from each pack call argument (e.g. `Box<int>` → `int`) rather than deducing the outer type directly (`Box<int>`).
    - `CallArgDeductionInfo` gained a new `function_pack_element_type_index` field populated by `buildDeductionMapFromCallArgs` when the pack element type is resolved.
    - `deduceTemplateArgsFromCall` uses that TypeIndex to look up the pack element's `TypeInfo`; when it is a template instantiation with the same base template as the call argument, it pairwise-extracts the inner (dependent) type rather than pushing the full call-arg type.
    - This fixes implicit calls like `f(box_int, box_dbl)` for `template<typename... Ts> int f(Box<Ts>... boxes)`: `Ts` is now correctly deduced as `{int, double}` instead of `{Box<int>, Box<double>}`.
    - Regression: `tests/test_implicit_deduction_box_pack_ret0.cpp`.
  - **2026-04-23 multi-type-pack implicit deduction guard landed:**
    1. `deduceTemplateArgsFromCall` now gates its variadic-Type branch on `function_pack_template_param_name`: a variadic type parameter that is NOT the function-parameter pack (e.g. `Tags...` in `template<typename... Tags, typename... Ts> f(Ts...)`) immediately produces an empty pack instead of consuming all call args. The symmetric guard was already present in `try_instantiate_template_explicit` (the explicit path).
    2. `try_instantiate_single_template` now populates `template_param_pack_sizes_` before the body re-parse (same save/restore `ScopeGuard` pattern as the explicit path), so `substituteTemplateParameters` can resolve `sizeof...(Tags) = 0` and `sizeof...(Ts) = 3` correctly via `get_template_param_pack_size` rather than the overcounting naive fallback.
    - Regression: `tests/test_multi_type_pack_implicit_deduction_ret0.cpp`.
  - **2026-04-23 multi-dependent pack element type deduction landed:**
    When a function-parameter pack's element type has **multiple** dependent template-parameter positions (e.g. `Pair<Ts, Us>...`), all of them are now correctly deduced from the corresponding positions inside each call argument.
    1. `CallArgDeductionInfo` gained a new `function_pack_dependent_param_names` set (in `Parser.h`), populated by `buildDeductionMapFromCallArgs` with **all** dependent names found in the pack element type's template arguments (not just the first). For simple `Ts... args` or `Box<Ts>... args` the set contains one name; for `Pair<Ts, Us>... args` it contains both.
    2. `deduceTemplateArgsFromCall` variadic-Type gate changed from a single-name equality check to a set-membership check, allowing each pack param (Ts, Us, …) to enter the call-arg consumption loop.
    3. The inner box-unwrapping loop now finds the position whose `dependent_name` matches the **current** pack parameter name rather than stopping at the first dependent position. This ensures `Ts` picks up position j=0 and `Us` picks up position j=1 from each `Pair<Ts,Us>` call argument.
    4. `try_instantiate_single_template`'s `template_param_pack_sizes_` population now uses `function_pack_call_arg_end - function_pack_call_arg_start` (the call-arg range count) for all packs in the dependent-names set, rather than `template_args.size() - non_variadic_count`. This prevents overcounting when multiple packs each contribute to `template_args`.
    5. The complex fold-expression substitution handler (`Parser_Templates_Substitution.cpp`) now prefers `pack_param_info_[0].pack_size` over `template_args.size() - non_variadic_count` for the expansion count, so fold expressions like `(0 + ... + (pairs.first + pairs.second))` expand the correct number of times.
    - Regression: `tests/test_multi_dep_pack_ret0.cpp`.
  - **What is still open:** nested template packs, other complex mapping shapes that may surface as the test corpus grows, and the broader substitution/metadata fallback cleanup cross-referenced below.

## What is clearly landed

### Phases 1-4 are complete

These are no longer the active work items:

- **Phase 1:** `NonTypeValueIdentity` is the canonical NTTP identity path.
- **Phase 2:** alias-template materialization now routes through shared helpers instead of duplicated ad hoc paths.
- **Phase 3:** `Parser.h` documents the late-materialization lifecycle and exposes the canonical registration helpers:
  - `registerLateMaterializedTopLevelNode(...)`
  - `registerAndNormalizeLateMaterializedTopLevelNode(...)`
  - `normalizePendingSemanticRootsIfAvailable()`
- **Phase 4:** `DependentPlaceholderKind` exists on `TypeInfo`, and the important placeholder consumers now use typed state instead of string heuristics.

### The first real Phase 5 slice is also done

The variable-declaration constructor path is no longer doing its own template-constructor materialization in stmt-decl codegen.

What changed:

- `src/IrGenerator_Stmt_Decl.cpp` now expects sema/materialized constructor information instead of instantiating template constructors itself.
- `src/SemanticAnalysis.cpp` now materializes the selected constructor earlier for the direct-init and brace-init variable-declaration paths.
- Nested/out-of-line constructor materialization for the exercised paths is covered by regression tests.

So the repo is **past “Phase 5 can begin”**. It has already begun.

### Slice A (conversion operators) is done

Sema's existence-check helper `structHasConversionOperatorTo` (used by `tryAnnotateConversion`) now takes a `SemanticAnalysis*` parameter and, when a matching lazy conversion operator is discovered in the source struct (or an inherited base struct), calls `ensureMemberFunctionMaterialized(struct, member, is_const)` as a side effect.

Concrete effect: every implicit `Struct→primitive` user-defined conversion that sema annotates is now guaranteed to have its operator body materialized **before** codegen begins. The codegen struct visitor sees the body and emits IR through the normal `visitFunctionDeclarationNode` path. Codegen's existing materialize-and-queue fallback in `emitConversionOperatorCall` still covers paths that bypass `tryAnnotateConversion` (explicit conversions, direct operator-name access).

Validation: full regression suite (`bash ./tests/run_all_tests.sh`) passes with the same 2170 pass / 147 expected-fail baseline.

## What is still open

### Remaining Phase 5 work

Phase 5 is now the remaining ownership cleanup: shrink the places where codegen still asks the parser to materialize lazy members on demand.

The main remaining surfaces are:

- `src/IrGenerator_Visitors_TypeInit.cpp` *(Slice D done; Slice E partially done — function-shaped deferred-queue fallback still reachable)*
- `src/IrGenerator_Call_Direct.cpp` *(historical slice; no longer an open Phase 5 surface)*
- `src/IrGenerator_Call_Indirect.cpp` *(historical slice; no longer an open Phase 5 surface)*
- `src/IrGenerator_MemberAccess.cpp` *(historical slice; no longer an open Phase 5 surface)*

These notes are now purely historical. The Phase 5 ownership cleanup is complete; the only remaining active roadmap item is Phase 6 explicit deduction.

### Remaining Phase 6 work

The materialization / ownership plan is effectively closed by the current tree state. The remaining follow-up is the **generalized explicit-deduction architecture**.

Covered by the current tree after this PR:

- trailing non-pack parameters after a function-parameter pack
- pack-before-tail signatures
- mixed explicit + deduced elements within the same function-parameter-pack slice
- the nondeduced-tail bug fixed by `tests/test_explicit_variadic_pack_nondeduced_tail_fail.cpp`
- template-parameter packs not mapped to any function-parameter pack (type and non-type), covered by `tests/test_explicit_type_pack_not_in_func_sig_ret0.cpp` and `tests/test_explicit_nontype_pack_not_in_func_sig_ret0.cpp`
- multiple variadic packs where only one is expanded in the function signature, covered by `tests/test_multi_pack_sizeof_deduction_ret0.cpp`
- fold expressions whose pack operand is a complex cast-expression rather than a bare identifier: `(... op (expr))` and `(init op ... op (expr))`, covered by `tests/test_fold_expr_complex_pack_ret0.cpp`
- **implicit deduction** of pack element types from template-specialisation function-parameter packs (`Box<Ts>...`): `Ts` is now correctly deduced as `{int, double}` when called as `f(Box<int>{}, Box<double>{})`, covered by `tests/test_implicit_deduction_box_pack_ret0.cpp`
- **multi-type-pack implicit deduction guard**: a variadic type parameter that is not the function-parameter pack now produces an empty pack (instead of consuming all call args). `template_param_pack_sizes_` is also populated by the implicit path so that `sizeof...` resolves correctly. Covered by `tests/test_multi_type_pack_implicit_deduction_ret0.cpp`.
- **multi-dependent pack element type deduction**: pack element types with multiple dependent positions (e.g. `Pair<Ts,Us>...`) now correctly deduce all packs. `CallArgDeductionInfo::function_pack_dependent_param_names` tracks all dependent names; `deduceTemplateArgsFromCall` gate and inner position-matching loop were updated; `template_param_pack_sizes_` uses the call-arg range count; complex fold expansion uses `pack_param_info_` size. Covered by `tests/test_multi_dep_pack_ret0.cpp`.
- **2026-04-23 nested wrapped pack deduction/materialization landed:** nested pack element shapes such as `Wrap<Box<Ts>>...` and `Wrap<Pair<Ts,Us>>...` now work across implicit deduction, mixed explicit+deduced calls, and class-template member/static-member function templates.
  1. `buildDeductionMapFromCallArgs` now recursively walks template-instantiation arguments instead of only the first template-arg layer, so nested dependent names populate `function_pack_dependent_param_names` and nested non-pack matches can pre-deduce concrete args.
  2. The variadic extraction paths (`deduceTemplateArgsFromCall`, `try_instantiate_template_explicit`, and `try_instantiate_member_function_template`) now recursively extract the concrete inner argument for the requested dependent pack parameter instead of relying on a flat top-level position.
  3. `substitute_template_parameter` now recursively materializes nested template-placeholder arguments while resolving concrete placeholder types, which fixes member-template parameter materialization for pack-expanded `Wrap<Pair<...>>` / `Wrap<Box<...>>` parameters.
  4. `instantiate_member_function_template_core` now rebuilds pack-parameter substitutions using the same nested dependent-name mapping, including correct parallel pack counts for co-packs.
  - Regressions: `tests/test_nested_box_pack_implicit_deduction_ret0.cpp`, `tests/test_nested_multi_dep_pack_ret0.cpp`, `tests/test_explicit_nested_multi_dep_pack_ret0.cpp`, `tests/test_class_template_nested_pack_member_ret0.cpp`.

Still open after this PR:

- nested template packs and other complex template-param ↔ function-param mapping shapes that may surface as the test corpus grows

### 2026-04-27 fallback-audit cross-check: template work not fully captured above

`docs\2026-04-27-fallback-comments-audit.md` identified additional template-instantiation fallback work that is related to this roadmap but broader than the Phase 5 materialization cleanup and the Phase 6 pack-deduction slices above. The answer to "is all remaining template-instantiation work described here?" is therefore **no**: the plan covered the known materialization and pack-mapping reproducers, but it did not explicitly track the remaining substitution/metadata fallback classes.

Add these as the next concrete roadmap items before starting the next PR:

1. **Authoritative substitution context**
   - Current symptom: fallback comments in `src\ExpressionSubstitutor.cpp`, `src\Parser_Templates_Lazy.cpp`, `src\Parser_Templates_Inst_ClassTemplate.cpp`, `src\Parser_Templates_Inst_Substitution.cpp`, and `src\Parser_Templates_Substitution.cpp` show dependent type/value/pack data being reconstructed after primary substitution loses it.
   - Missing feature: one substitution context object should carry type parameters, non-type parameter values, pack slices/sizes, current-instantiation identity, namespace/member context, and SFINAE/error-mode state through every instantiation entry point.
   - Removal target: general "substitute remaining template parameters" fallbacks, secondary ExpressionSubstitutor catch-alls, pack-size overcount formulas, and name-based re-discovery after a scope has exited.
   - Activity note: a hard-fail probe of the `src\Parser_Templates_Inst_ClassTemplate.cpp` fallback that reuses an unresolved class-template `TypeSpecifierNode` as-is broke deferred-base / pack-expansion / dependent-member cases including `tests\test_nttp_base_class_substitution_ret0.cpp`, `tests\test_pack_expansion_base_class_ret0.cpp`, `tests\test_ratio_negative_lazy_member_ret0.cpp`, and `tests\test_type_traits_dependent_member_nttp_ret42.cpp`. This path is active and should be root-fixed via better substitution metadata, not removed directly.
   - Activity note: in `src\Parser_Templates_Substitution.cpp`, hard-failing the non-type fold-pack reconstruction path broke `tests\test_fold_nontype_ret42.cpp`, and hard-failing the `sizeof...` `get_pack_size(pack_name)` rescue broke `tests\test_explicit_template_pack_sizeof_param_name_ret0.cpp`; both are active signs that pack identity/size metadata is still getting lost before substitution finishes.
   - Activity note: the more authoritative `sizeof...` `get_template_param_pack_size(pack_name)` branch in the same file is also active; hard-failing it broke `tests\test_explicit_multi_dep_pack_ret0.cpp`, `tests\test_explicit_nontype_pack_not_in_func_sig_ret0.cpp`, `tests\test_explicit_template_pack_fill_c_varargs_ret0.cpp`, `tests\test_explicit_template_pack_sizeof_param_name_ret0.cpp`, `tests\test_explicit_type_pack_not_in_func_sig_ret0.cpp`, `tests\test_explicit_variadic_pack_deduction_ret0.cpp`, `tests\test_multi_dep_pack_ret0.cpp`, `tests\test_multi_pack_sizeof_deduction_ret0.cpp`, `tests\test_multi_type_pack_implicit_deduction_ret0.cpp`, `tests\test_nested_multi_dep_pack_ret0.cpp`, and `tests\test_pack_nonpack_mixed_explicit_deduction_ret0.cpp`.
   - Activity note: the broader `sizeof...` template-argument reconstruction path in the same file is also active; hard-failing it broke `tests\test_explicit_template_pack_sizeof_param_name_ret0.cpp`, `tests\test_method_on_temporary_ret0.cpp`, `tests\test_pack_expansion_in_template_body_ret0.cpp`, `tests\test_sizeof_pack_class_template_ret0.cpp`, `tests\test_sizeof_pack_name_match_ret0.cpp`, `tests\test_sizeof_pack_namespace_member_template_ret0.cpp`, `tests\test_template_sizeof_pack_ret3.cpp`, and `tests\test_var_template_variadic_primary_ret42.cpp`.
   - Activity note: the narrower fold-expression `pack_param_info_` fallback in the same file was hard-fail probed and the full suite still passed, so that dead secondary fold-pack rescue has already been removed.

2. **Canonical template argument metadata on TypeInfo/TemplateRegistry**
   - Current symptom: several paths recover template args from TypeInfo names, stripped qualifiers, stale TypeIndex values, or string-based instantiation names.
   - Missing feature: every template instantiation TypeInfo should be born with canonical template arguments, base-pattern identity, and pattern-to-instantiation links that all consumers can query without string parsing.
   - Removal target: `ExpressionSubstitutor` name-based template-arg recovery, `$`/base-name string stripping, stale-TypeIndex fallback lookup, and manual prefix scans for instantiated types.
   - Activity note: the old non-variadic base-template "assume `template_args[0]`" fallback in `src\Parser_Templates_Inst_ClassTemplate.cpp` was hard-fail probed and the full suite still passed, so that dead base-argument shortcut has already been removed.
   - Activity note: the base-class instantiation-name fallback in `src\Parser_Templates_Inst_Substitution.cpp` was also hard-fail probed and the full suite still passed, so the remaining corpus no longer needs the old "use basic name without defaults" rescue there.
   - Activity note: the old `sizeof(T)` / `alignof(T)` type-name mapping fallback in `src\Parser_Templates_Inst_Substitution.cpp` has now been removed. The root cause was in `src\Parser_Templates_Class.cpp`, which registered temporary `TypeInfo` entries for class-template type parameters but did not copy the resulting `type_index` back onto the `TemplateParameterNode`. After wiring `set_registered_type_index(...)` there, hard-failing the old name-based mapping path still passed `tests\test_dependent_sizeof_alignof_template_arg_ret0.cpp` and the full suite.
   - Activity note: the instantiated-member-alias copy fallback in `src\Parser_Templates_Inst_Substitution.cpp` was hard-fail probed and the full suite still passed, so that dead alias-copy branch has already been removed.

3. **Explicit dependent-placeholder state everywhere**
   - Current status: Phase 4 introduced `DependentPlaceholderKind`, but fallback comments still show sentinel-style dependent detection such as `type_index == 0` and placeholder TypeSpec recovery.
   - Missing feature: all dependent type, member type, NTTP, and pack placeholders should carry explicit kind/name/binding metadata from parse through substitution and sema.
   - Removal target: `type_index == 0` dependent checks, "placeholder struct type" returns, and fallback TypeInfo lookups by parameter name.
   - Activity note: a hard-fail probe of the `type_index == 0` dependent-marking path in `src\Parser_Templates_Params.cpp` broke `comparison_operators_ret1.cpp`, `float_comparisons_ret1.cpp`, `spaceship_basic_ret253.cpp`, `test_const_member_with_param_ret255.cpp`, `test_decltype_function_template_base_ret42.cpp`, `test_float_cmp_ret6.cpp`, `test_operator_overload_template_ret40.cpp`, `test_spaceship_3member_ret15.cpp`, `test_spaceship_custom_ordering_ret3.cpp`, `test_spaceship_multi_member_ret8.cpp`, `test_spaceship_precedence_ret42.cpp`, and `test_spaceship_synthesized_ret8.cpp`. This sentinel path is still active and must be replaced by explicit placeholder metadata before removal.
   - Activity note: the old string-parsing dependent-template-placeholder rescue in `src\Parser_Core.cpp` was removed and the full suite still passed, so the current corpus now relies on canonical `TypeInfo` metadata rather than base-name extraction for this check.
   - Activity note: the narrower `type_name = full_type_name` rescue in `src\Parser_Templates_Params.cpp` is also still active; hard-failing it broke `tests\test_variable_template_in_enable_if_ret0.cpp`, so parsed template-argument types can still arrive without a usable token spelling in current dependent-variable-template cases.

4. **Non-type template argument/default evaluation path**
   - Current symptom: class-template instantiation falls back from specialized handlers to `ConstExprEvaluator` for non-type defaults, variable templates, array dimensions, and directly evaluated expressions.
   - Missing feature: NTTP substitution should preserve value category, converted constant-expression type, and parameter identity before default filling or SFINAE checks run.
   - Removal target: catch-all non-type default evaluation fallbacks and "template may still work with fallback path" comments.
   - Activity note: a hard-fail probe of the unresolved-default catch-all in `src\Parser_Templates_Inst_ClassTemplate.cpp` broke `tests\test_template_template_default_ret42.cpp`, so this fallback is active in the current corpus and needs a root fix before removal.
   - Activity note: a hard-fail probe of the narrower `tryAppendEvaluatedTemplateValue(...)` non-type-default fallback broke `tests\test_expr_subst_noexcept_wrap_ret0.cpp`, `tests\test_template_spec_outofline_default_arg_ret42.cpp`, and `tests\test_template_spec_outofline_default_arg_namespaced_ret42.cpp`, so the specialized default handlers still do not cover all NTTP default forms.
   - Activity note: the variable-template constexpr bridge in the same area is also active; hard-failing it broke `tests\test_variable_template_nttp_base_class_ret0.cpp`.
   - Activity note: the deferred-base direct `try_evaluate_constant_expression(arg_info.node)` fallback in the same file was hard-fail probed and the full suite still passed, so that dead direct-evaluation rescue has already been removed.
   - Activity note: the unresolved deferred-base `TypeSpecifierNode` passthrough in the same file is also active; hard-failing it broke `tests\test_nttp_base_class_substitution_ret0.cpp`, `tests\test_pack_expansion_base_class_ret0.cpp`, `tests\test_pack_expansion_member_type_base_ret0.cpp`, `tests\test_ratio_equal_deferred_base_ret1.cpp`, `tests\test_ratio_negative_lazy_member_ret0.cpp`, `tests\test_ternary_deferred_base_chained_ret0.cpp`, `tests\test_ternary_deferred_base_ret0.cpp`, and `tests\test_type_traits_dependent_member_nttp_ret42.cpp`.

5. **Initializer/static-member substitution ownership**
   - Current symptom: class-template instantiation still has general fallback passes for initializers and static members from AST nodes/patterns/specializations.
   - Missing feature: member/static-member declarations and initializers should be substituted through the same canonical context as function bodies, with instantiated ownership recorded before sema/codegen.
   - Removal target: "General fallback: substitute remaining template parameters in the initializer" and "Fallback: Process static members from AST node" style paths.
   - Activity note: the array-dimension substitution fallback used when `resolve_array_dimensions(...)` produced no dimensions but `substituted_array_size` was present was hard-fail probed and the full suite still passed, so that dead array-dimension fallback has already been removed from `src\Parser_Templates_Inst_ClassTemplate.cpp`.
   - Activity note: the old AST-node static-member fallback in `src\Parser_Templates_Inst_ClassTemplate.cpp` was hard-fail probed and the full suite still passed, so that dead secondary static-member path has already been removed.
   - Activity note: the broader static-member initializer substitution path in the same file is still active; hard-failing it broke a large cluster including `template_ttp_static_constexpr_member_ret0.cpp`, `test_array_enable_if_deduction_ret0.cpp`, `test_dependent_template_instantiation_ret0.cpp`, `test_static_members_template_ret0.cpp`, `test_template_static_member_outofline_ret42.cpp`, and `test_var_template_inner_impl_defaulted_outer_arg_ret42.cpp`.

6. **ExpressionSubstitutor role clarification**
   - Current symptom: ExpressionSubstitutor is used both as an expected AST rewrite mechanism and as a late fallback after other substitution paths did not handle a node.
   - Missing feature: define ExpressionSubstitutor's preconditions and make it consume the authoritative substitution context. It should not reconstruct bindings from global TypeInfo or names.
   - Removal target: fallback creation of ad-hoc expression nodes and fallback recovery of template args from type names.
   - Activity note: the `sizeof...` class-template pack-context rescue in `src\Parser_Templates_Substitution.cpp` was hard-fail probed and the full suite still passed, so that dead name/context rediscovery path has already been removed.
   - Activity note: the analogous lazy static-member ExpressionSubstitutor path in `src\Parser_Templates_Lazy.cpp` is still active; hard-failing it broke a broad cluster including `template_ttp_static_constexpr_member_ret0.cpp`, `test_alias_base_static_member_ret0.cpp`, `test_integral_constant_simple_ret30.cpp`, `test_ratio_lazy_static_member_ret0.cpp`, `test_template_static_member_initializer_scalar_brace_ret42.cpp`, and `test_type_traits_dependent_member_nttp_ret42.cpp`.
   - Activity note: the unknown-member-function direct-copy fallback in `src\Parser_Templates_Inst_ClassTemplate.cpp` was hard-fail probed and the full suite still passed, so that dead branch has already been removed.

8. **Function template declaration/body reconstruction**
   - Current symptom: function-template instantiation in `src\Parser_Templates_Inst_Deduction.cpp` still falls back to older return-type substitution and direct body-pointer reuse when the newer declaration/body reparse path does not fully reconstruct the instantiated function.
   - Missing feature: the reparse path should always produce a fully instantiated declaration signature and body ownership, including concepts, trailing return types, pack-heavy signatures, and forward-declared definitions.
   - Removal target: `Fallback: Use simple substitution (old behavior)` and `Fallback: copy the function body pointer directly (old behavior)`.
   - Activity note: hard-failing the return-type substitution path broke a broad cluster including `concept_abbreviated_ret0.cpp`, `concept_comprehensive_ret15.cpp`, `template_inst_simple_ret5.cpp`, `test_func_template_dependent_default_nontype_sizeof_ret0.cpp`, and `test_nested_pack_return_type_ret42.cpp`.
   - Activity note: hard-failing the direct body-copy path broke `decltype_trailing_return_ret0.cpp`, `test_dependent_swap_decltype_noexcept_ret0.cpp`, `test_namespaced_pair_swap_sfinae_ret0.cpp`, `test_std_swap_enable_if_alias_base_ret0.cpp`, and `test_template_template_forward_decl_definition_ret0.cpp`.

7. **Intra-instantiation call-target rewriting remains important**
   - This is already diagnosed in the validation-history section around Slice G/H: `ExpressionSubstitutor` does not rewrite intra-struct call targets in instantiated member bodies from the template pattern declaration to the instantiated member stub.
   - Keep this item attached to the substitution-context work, not the materialization-drain work. The materialization drain is largely solved; the remaining problem is preserving declaration identity during substitution.

These items should be handled as a **Phase 7: substitution metadata unification** rather than more Phase 5 materialization work. Phase 6 can stay focused on pack-deduction/mapping reproducers; Phase 7 should remove the fallback classes that reconstruct template facts after the fact.

## Clear next steps

1. **Keep the deduction regression cluster close.**
   - `tests/test_explicit_variadic_pack_deduction_ret0.cpp`
   - `tests/test_explicit_variadic_pack_nondeduced_tail_fail.cpp`
   - `tests/test_explicit_variadic_pack_trailing_default_ret0.cpp`
   - `tests/test_pack_nonpack_mixed_explicit_deduction_ret0.cpp`
   - `tests/test_explicit_template_pack_sizeof_param_name_ret0.cpp`
   - `tests/test_implicit_deduction_box_pack_ret0.cpp`
   - `tests/test_multi_type_pack_implicit_deduction_ret0.cpp`
   - `tests/test_multi_dep_pack_ret0.cpp`

## Recommended interpretation of the roadmap

If you want the shortest accurate summary:

- **Done:** Phases 1-4
- **Done:** Phase 5 materialization / ownership cleanup
- **In progress:** Phase 6 explicit-deduction cleanup
- **Landed in Phase 6 so far:** the nondeduced-tail positional-fallback fix, canonical function-parameter → call-argument pack-slice metadata, pack-before-tail explicit deduction, mixed explicit + deduced pack support, multi-pack sizeof resolution, fold expressions with complex pack operands, implicit deduction of inner types from template-specialisation pack parameters, **multi-type-pack implicit deduction guard** (non-function type packs produce empty packs; `template_param_pack_sizes_` now populated by the implicit path), **multi-dependent pack element type deduction** (`Pair<Ts,Us>...` deduces both packs correctly), **nested wrapped pack deduction/materialization** (`Wrap<Box<Ts>>...` / `Wrap<Pair<Ts,Us>>...` now work in free and member templates)
- **Work left after this PR:** nested template packs and other complex mapping shapes that may surface as the test corpus grows; plus **Phase 7 substitution metadata unification** from the 2026-04-27 fallback-audit cross-check.

## Suggested agent prompt for next PR planning

Use this prompt if more detail is needed before implementation:

> Read `docs\2026-04-08-template-instantiation-materialization-plan.md` and `docs\2026-04-27-fallback-comments-audit.md`. Focus only on template-instantiation fallback cleanup, especially the new "2026-04-27 fallback-audit cross-check" / proposed Phase 7 section. Audit the current source for the listed fallback classes in `ExpressionSubstitutor.cpp`, `Parser_Templates_Lazy.cpp`, `Parser_Templates_Inst_ClassTemplate.cpp`, `Parser_Templates_Inst_Substitution.cpp`, `Parser_Templates_Substitution.cpp`, and `Parser_Templates_Params.cpp`. Produce a concrete implementation slice plan that identifies the first fallback class to remove, the exact source paths/functions involved, the regression test to add first, and the invariant that should replace the fallback. Do not implement until the plan names one narrow slice and explains how it avoids changing unrelated template behavior.

## Regression coverage worth keeping close

The following tests are the most relevant guardrails for this area:

- `tests/test_pending_sema_normalization_ret0.cpp`
- `tests/test_template_nested_ctor_materialized_before_codegen_ret42.cpp`
- `tests/test_sfinae_dependent_member_ret0.cpp`
- `tests/test_dependent_alias_chain_placeholder_ret42.cpp`
- `tests/test_sizeof_dependent_member_type_ret8.cpp`
- `tests/test_placeholder_kind_mixed_types_ret100.cpp`
- `tests/test_conv_op_sema_phase5_ret42.cpp`
- `tests/test_phase5_nested_templates_ret42.cpp`
- `tests/test_phase5_multi_level_ret45.cpp`
- `tests/test_explicit_variadic_pack_deduction_ret0.cpp`
- `tests/test_explicit_variadic_pack_nondeduced_tail_fail.cpp`
- `tests/test_explicit_variadic_pack_trailing_default_ret0.cpp`
- `tests/test_pack_nonpack_mixed_explicit_deduction_ret0.cpp`
- `tests/test_explicit_template_pack_sizeof_param_name_ret0.cpp`
- `tests/test_fold_expr_complex_pack_ret0.cpp`
- `tests/test_implicit_deduction_box_pack_ret0.cpp`
- `tests/test_multi_type_pack_implicit_deduction_ret0.cpp`
- `tests/test_multi_dep_pack_ret0.cpp`
- `tests/test_nested_box_pack_implicit_deduction_ret0.cpp`
- `tests/test_nested_multi_dep_pack_ret0.cpp`
- `tests/test_explicit_nested_multi_dep_pack_ret0.cpp`
- `tests/test_class_template_nested_pack_member_ret0.cpp`

## Validation baseline refreshed on 2026-04-23 (Phase 6: multi-dependent pack element type deduction)

Linux validation run after:
- Adding `function_pack_dependent_param_names` set to `CallArgDeductionInfo` in `Parser.h`.
- Populating the set with ALL dependent names in the pack element type in `buildDeductionMapFromCallArgs` (removing the early `break` that stopped at the first dependent name).
- Changing the `deduceTemplateArgsFromCall` variadic-Type gate from single-name equality to set-membership check.
- Fixing the inner box-unwrapping loop to find the position matching the current pack parameter name, enabling `Pair<Ts,Us>...` to extract `Ts` from position j=0 and `Us` from position j=1.
- Fixing `try_instantiate_single_template` pack-sizes population to use `function_pack_call_arg_end - function_pack_call_arg_start` for all packs in the set (not `template_args.size() - non_variadic_count`).
- Fixing the complex fold-expression substitution handler to prefer `pack_param_info_[0].pack_size` over `template_args.size() - non_variadic_count` for expansion count.
- Adding regression test `tests/test_multi_dep_pack_ret0.cpp`.

Run:

- `make sharded CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline:

- **2186** compile+link+runtime passing tests (+1 from new `test_multi_dep_pack_ret0.cpp`)
- **149** `_fail` tests failing as expected
- overall result: **SUCCESS**



Linux validation run after:
- Adding `function_pack_element_type_index` field to `CallArgDeductionInfo` in `Parser.h`.
- Setting `function_pack_element_type_index` in `buildDeductionMapFromCallArgs` when the pack element type is a resolved TypeIndex.
- Extending `deduceTemplateArgsFromCall` variadic type-param branch to extract inner types from template-specialisation pack call arguments using the stored TypeIndex.
- Adding regression test `tests/test_implicit_deduction_box_pack_ret0.cpp`.

Run:

- `make sharded CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline:

- **2184** compile+link+runtime passing tests (+1 from new `test_implicit_deduction_box_pack_ret0.cpp`)
- **149** `_fail` tests failing as expected
- overall result: **SUCCESS**

## Validation baseline refreshed on 2026-04-23 (Phase 6: fold-expression complex-pack parser fix)

Linux validation run after:
- Extending fold-expression Pattern 1 `(... op cast-expr)` to accept non-bare-identifier pack operands in the parser.
- Extending fold-expression Pattern 3 `(init op ... op cast-expr)` similarly.
- Adding `FoldExpressionNode` constructor for binary fold with complex pack expression.
- Adding regression test `tests/test_fold_expr_complex_pack_ret0.cpp`.

Run:

- `make sharded CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline:

- **2183** compile+link+runtime passing tests (+1 from new `test_fold_expr_complex_pack_ret0.cpp`)
- **149** `_fail` tests failing as expected
- overall result: **SUCCESS**

Landed on top of the drain-by-ODR-use pass:
- `SemanticAnalysis::drainLazyMemberRegistry` now calls `normalizeTopLevelNode(*result)` immediately after each successful `ensureMemberFunctionMaterialized` (in both the AST-walk pass and the ODR-use pass). This guarantees a freshly-substituted body has its internal call expressions annotated by sema (routed through `tryAnnotateCallArgConversions` → `tryMaterializeLazyCallTarget`). The normalize helpers dedup via `normalized_bodies_`, so re-normalizing an already-processed node is a safe no-op.

Deeper diagnosis of the residual 4 tests / 10 forwarder hits (unchanged count after the re-normalization change):
- The hits are always calls inside a freshly-materialized body (e.g. `helper()` / `other.method(...)` inside `Box<int>::compute()`).
- Instrumenting `tryMaterializeLazyCallTarget` reveals the real cause: when sema annotates those inner calls, it resolves them to the **template pattern's** declaration (`Box::helper`, `Box::method`), **not** to the instantiated struct's lazy stub (`Box$3ee5c699332008a6::helper`). The template pattern has its body, so `tryMaterializeLazyCallTarget` correctly short-circuits (`func_decl->get_definition().has_value() == true`). No lazy materialization is triggered at sema time, so the codegen forwarder is legitimately the first materializer when codegen lowers the instantiated receiver.
- This is a **substitution-layer gap**, not a materialization-layer gap: `ExpressionSubstitutor` does not rewrite intra-struct call targets to point at the instantiated struct's stubs. Fixing this properly means teaching the substitutor to redirect any `CallExprNode` whose resolved target is a member of the owning template pattern to the corresponding member of the instantiated struct (before sema runs), or alternatively teaching `tryMaterializeLazyCallTarget` to map a pattern-resolved call to the active instantiation via `member_context_stack_`.
- Scope of that fix is significantly larger than Slice G and deserves a separate slice (call it Slice H: "intra-instantiation call-target rewriting"). Until then, the codegen forwarder remains the correct and minimal bridge for these 10 hits.

Run:

- `.\build_flashcpp.bat`
- `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`

Current baseline:

- **2201** compile+link+runtime passing tests
- **148** `_fail` tests failing as expected
- overall result: **SUCCESS**

## Validation baseline refreshed on 2026-04-21 (Slice G: ODR-use drain extension + audit log)

Landed on top of the Slice G foundation:
- `LazyMemberInstantiationRegistry::snapshotOdrUsedLazyEntries()` — returns a snapshot of `(owner, member, is_const)` triples for lazy entries currently marked ODR-used. Snapshot (not live view) because materialization mutates the map.
- `SemanticAnalysis::drainLazyMemberRegistry` gained a second fixpoint pass after the AST-walk pass that materializes the ODR-used residuals via `ensureMemberFunctionMaterialized`. Safe against SFINAE-probed instantiations by construction: they never flow through a sema annotation site that calls `markOdrUsed`.
- `AstToIr::materializeLazyMemberIfNeeded` gained a debug-level audit log (`Codegen:Debug`) that fires when the forwarder is still the first materializer for a lazy member.

Audit results (run against the full test corpus with `--log-level=Codegen:debug`):
- **4 tests, 10 first-materializer hits remain.** All hits are inner calls inside freshly-materialized bodies (e.g. calls to `method` / `helper` inside `Box<int>::compute()`'s body).
- Root cause: `drainLazyMemberRegistry` runs *after* `normalizePendingSemanticRoots`, so the freshly-substituted bodies created by the drain never get their internal `tryAnnotateCallArgConversions` pass. That means calls inside those bodies never reach `tryMaterializeLazyCallTarget`, never get `markOdrUsed`, and are left for the codegen-side forwarder to resolve on demand.
- This is the clear next architectural step: interleave sema annotation with the drain (either run `normalizePendingSemanticRoots` after the drain, or loop sema+drain to a fixpoint) so that newly-substituted bodies also get annotated before codegen runs. That would either eliminate the forwarder hits entirely or narrow them to an even smaller residual worth diagnosing.

Run:

- `.\build_flashcpp.bat`
- `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`

Current baseline (unchanged by the drain extension):

- **2201** compile+link+runtime passing tests
- **148** `_fail` tests failing as expected
- overall result: **SUCCESS**

## Validation baseline refreshed on 2026-04-21 (Slice G foundation: explicit ODR-use plumbing)

Windows validation was re-run after landing:
- `LazyMemberInstantiationRegistry::markOdrUsed` / `isOdrUsed` / `odr_used_` set (with `...Any` variants and `clear()` integration) in `src/TemplateRegistry_Lazy.h`.
- `markOdrUsed` calls at three non-speculative sema sites:
  - `structHasConversionOperatorTo` in `src/SemanticAnalysis.cpp` (Slice A / tryAnnotateConversion path).
  - `tryMaterializeLazyCallTarget` in `src/SemanticAnalysis.cpp` (Slice B/C / direct + indirect call target).
  - `ensureSelectedConstructorMaterialized` in `src/SemanticAnalysis.cpp` (ctor selection).
- `ensureMemberFunctionMaterialized` intentionally left neutral — it is reachable from codegen / constexpr forwarders whose semantics are weaker than "sema proved ODR-use".

Run:

- `.\build_flashcpp.bat`
- `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`

Current baseline:

- **2201** compile+link+runtime passing tests
- **148** `_fail` tests failing as expected
- overall result: **SUCCESS**

The foundation is in place for the drain-by-ODR-use extension to land as a separate follow-up commit (see step 2 under "Clear next steps").

## Validation baseline refreshed on 2026-04-22 (Slice F finalization; Phase 6 positional-fallback fix)

Windows validation was re-run after:
- reverting an attempted `try_instantiate_class_template` side-list tracking + drain extension that regressed `tests/test_namespaced_pair_swap_sfinae_ret0.cpp` (template arguments to SFINAE probes are instantiated in non-SFINAE context, so gating on `in_sfinae_context_` was insufficient — their member bodies are lazily ill-formed and must not be eagerly drained);
- removing the temporary audit instrumentation from `AstToIr::materializeLazyMemberIfNeeded`;
- rewriting the comments in `drainLazyMemberRegistry` and `materializeLazyMemberIfNeeded` to document the intentional scope of the drain and why the forwarder is retained;
- landing the Phase 6 positional-fallback fix in `try_instantiate_template_explicit` (gate on `has_variadic_func_pack`, skip pre-deduced call-arg slots) with new regression test `tests/test_explicit_variadic_pack_nondeduced_tail_fail.cpp`.

Run:

- `.\build_flashcpp.bat`
- `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`

Current baseline:

- **2201** compile+link+runtime passing tests
- **148** `_fail` tests failing as expected (+1 from new `test_explicit_variadic_pack_nondeduced_tail_fail.cpp`)
- overall result: **SUCCESS**

Slice F is complete in its landing form: the sema-owned drain is the first materializer for every struct reachable from `parser_.get_nodes()`, and the residual codegen forwarder is an on-demand bridge that delegates to sema rather than performing materialization itself.

## Validation baseline refreshed on 2026-04-21 (Slice F partial completion)

Linux validation was re-run after adding `SemanticAnalysis::drainLazyMemberRegistry` and collapsing `AstToIr::materializeLazyMemberIfNeeded` to a pure sema forwarder:

- `make main CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline:

- **2171** compile+link+runtime passing tests
- **147** `_fail` tests failing as expected
- overall result: **SUCCESS**

A temporary audit guard was inserted in the forwarder that threw `InternalError` whenever sema was about to perform first-time materialization at codegen time. With only Slices A-E in place it caught 34 struct-visitor tests (confirming the struct-visitor was the remaining codegen-first site). After Slice F's drain was added, the same guard fell to 4 tests — all of them involving instantiated structs that live outside the top-level AST walk. Those residual tests are the only reason the forwarder still sees first-time work; they are the tracking item for step 3's outright deletion.

## Validation baseline refreshed on 2026-04-20 (Slice E completion)

Linux validation was re-run after removing the function-shaped deferred-queue fallback:

- `make main CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline:

- **2171** compile+link+runtime passing tests
- **147** `_fail` tests failing as expected
- overall result: **SUCCESS**

An audit guard that converted the removed fallback into a hard `InternalError` was run against the full suite before deletion and was never triggered, confirming the fallback is genuinely unreachable with the new sema-first materialization at the queue-seeding sites.

## Related docs

- `docs/2026-04-06-template-constexpr-sema-audit-plan.md`
- `docs/2026-03-21_PARSER_TEMPLATE_SEMA_BOUNDARY_PLAN.md`
- `docs/2026-04-04-codegen-name-lookup-investigation.md`

## PR #1344 review comments addressed (2026-04-23)

Four review comments from PR #1344 were addressed:

### Fix 1: Tab/space indentation (Devin)
`src/Parser_Templates_Inst_Deduction.cpp` lines 1006-1012 had mixed leading
space + tab characters.  Replaced with pure tab indentation to match the rest
of the file.

### Fix 2: Namespace/template-specialisation pack name extraction (Gemini)
`src/Parser_Templates_Inst_Deduction.cpp` — the pack-parameter name extractor
used `fp_type.token().handle()` unconditionally.  For simple `Ts... args` that
token correctly returns `Ts`, but for `MyBox<Ts>... args` it returns `MyBox`
(the template name), not the pack parameter `Ts`.

The fix: after reading the token handle, check whether it exists in
`tparam_nodes_by_name`.  If it does not (or is invalid), walk the `TypeInfo`
for the type specifier; if the type is a template instantiation, iterate its
`templateArgs()` and use the first entry whose `dependent_name` is valid as the
pack parameter name.  The stale `FLASH_LOG_FORMAT(Templates, Error, ...)`
diagnostic that fired for every template-instantiation type was removed because
it incorrectly flagged valid cases as errors.

A new test `tests/test_explicit_pack_template_specialization_ret0.cpp` was
added that calls `f<0>(a, b)` where `f` is declared
`template<int N, typename... Ts> int f(Box<Ts>... boxes)`.

### Fix 3: ScopeGuard ordering for exception safety (Gemini)
`src/Parser_Templates_Inst_Deduction.cpp` — `ScopeGuard
restore_template_pack_sizes` was constructed *after* the loop that populates
`template_param_pack_sizes_`.  If the loop threw (e.g., `std::bad_alloc`) the
saved data would be permanently lost.  Moved the `ScopeGuard` construction to
immediately after the `std::move` and before the population loop.

### Fix 4: Empty pack falling through to naive fallback (Devin)
`src/Parser_Templates_Substitution.cpp` — when
`get_template_param_pack_size` returned `0` for an authoritative empty pack,
`found_variadic` was set to `true` but `num_pack_elements` stayed `0`, causing
the next `if (num_pack_elements == 0)` guard to re-enter the naive
`template_args.size() - non_variadic_count` formula.  This overcounts when
multiple variadic packs are present.  Changed the guard condition to
`if (num_pack_elements == 0 && !found_variadic)` so that an authoritative
empty-pack result is respected and the naive fallback is skipped.

### Validation baseline (2026-04-23)

- `make main CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline:

- **2182** compile+link+runtime passing tests (includes new test)
- **149** `_fail` tests failing as expected
- overall result: **SUCCESS**

## Phase 6 continued: inner member function pack in class-template context (2026-04-23)

Fixed the first entry in `docs/KNOWN_ISSUES.md` ("inner member function pack
in class template context — fold returns only first pack element").

### Root cause

When a class template contains an inner member function template with its own
variadic type pack — e.g.:

```cpp
template<typename... Ts>
struct Wrapper {
    template<typename... Us>
    int call(Us... args) { return (0 + ... + args); }
};
```

…the pattern `DeclarationNode` for `Us... args` on the member template did not
have `is_parameter_pack` set, and its type specifier's `token()` was empty
(the type is stored as a `TypeIndex` pointing to the template-parameter
`TypeInfo` for `Us`, not a named token).

Two downstream consequences:

1. `Parser::buildDeductionMapFromCallArgs` gated pack detection solely on
   `DeclarationNode::is_parameter_pack()` and so never populated
   `function_pack_call_arg_start/end`,
   `function_pack_dependent_param_names`, or
   `function_pack_template_param_name` for the inner pack.
2. `Parser::try_instantiate_member_function_template` had a naive
   per-template-param deduction loop that pushed exactly one
   `TemplateTypeArg` per template parameter, so the variadic `Us` was
   deduced with 1 element regardless of the call arity.

The combination caused `try_instantiate_member_function_template` to return a
single-element `Us = {int}` even for `w.call(10, 15, 17)`.  Downstream, the
MemberFunc parameter-expansion path only created `args_0` in the symbol
table, so `count_pack_elements("args")` returned `1` and the unary fold
`(0 + ... + args)` expanded to a single element.

### Fix

Two small, scoped changes:

- `src/Parser_Templates_Inst_Deduction.cpp / buildDeductionMapFromCallArgs`
  — detect packs by either (a) `is_parameter_pack()` or (b) the type
  specifier naming a variadic template parameter of the enclosing template
  (mirroring the fallback that already exists in
  `instantiate_member_function_template_core`).  Additionally, when
  extracting the pack's template-parameter name, fall back to the
  `TypeInfo::name()` when the type token handle is invalid, so that the
  inner pack's name (e.g. `Us`) is correctly recorded in
  `function_pack_dependent_param_names` /
  `function_pack_template_param_name`.
- `src/Parser_Templates_Inst_MemberFunc.cpp / try_instantiate_member_function_template`
  — handle variadic Type template parameters in the deduction loop by
  consuming the `function_pack_call_arg_start..function_pack_call_arg_end`
  slice (matching the variadic branch of `deduceTemplateArgsFromCall`),
  producing an empty pack for variadic template params that do not map to
  the function-parameter pack.

Regression test: `tests/test_class_template_inner_func_pack_fold_ret0.cpp`
exercises simple and multi-pack inner fold cases including a non-variadic
outer class template, verifying that all three inner-pack folds return the
expected sum.

### Validation baseline (2026-04-23 · afternoon)

- `make sharded CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline (Linux/clang):

- **2193** compile+link+runtime passing tests (+11 vs morning baseline; the
  additional passing tests are existing tests whose inner-pack fold or
  member-function pack deduction was previously incorrect)
- **149** `_fail` tests failing as expected
- overall result: **SUCCESS**

### Still open Phase 6 items (unchanged)

- ~~Static member function template via `S<T1,T2>::f(args...)` — body not
  queued for codegen; link fails with undefined reference.~~ (fixed — see below)
- Unsized array with pack-expanded initializer keeps pre-expansion size 1.
- Aggregate brace-init of a struct with pack-expanded initializer parse
  failure at the call site.

## Phase 6 continued: static member function template qualified call (2026-04-23)

Fixed the second entry in `docs/KNOWN_ISSUES.md` ("static member function
template — qualified call leaves body unmaterialized").

### Root cause (two interlocking bugs)

1. `parse_template_function_declaration_body` (in `Parser_Templates_Function.cpp`)
   parsed `static` via `parse_declaration_specifiers()` but only propagated
   `constexpr/consteval/constinit`.  The `StorageClass::Static` was silently
   dropped, so the template pattern's `is_static()` was false.  As a result
   `copy_function_properties` never set `is_static` on the instantiated
   function, causing codegen to emit an implicit `this` register save and
   shifting all real parameters by one (wrong ABI).

2. `try_parse_member_template_function_call` (in `Parser_Expr_QualLookup.cpp`)
   never called `try_instantiate_member_function_template` (the
   argument-deduction path) for qualified static calls.  It only tried
   `try_instantiate_member_function_template_explicit` (explicit template args
   or zero-arg case) and the lazy fallback (which handles non-template members
   only).  When call args were present but no explicit template args, no
   instantiation path was taken, leaving an undefined-reference at link time.

### Fixes

- `src/Parser_Templates_Function.cpp / parse_template_function_declaration_body`
  — after applying `constexpr/consteval/constinit`, also apply
  `StorageClass::Static` from `specs.storage_class`.
- `src/Parser_Expr_QualLookup.cpp / try_parse_member_template_function_call`
  — add a third branch (in addition to explicit-args and zero-args) that
  collects arg types via `get_expression_type` and calls
  `try_instantiate_member_function_template`.  This mirrors exactly the
  `Parser_Expr_PostfixCalls.cpp` handling for regular (non-static) member
  template calls.

Regression test: `tests/test_class_template_static_member_func_template_ret0.cpp`
covers variadic/non-variadic combinations of outer class template and inner
static member function template.

### Validation baseline (2026-04-23 · evening)

- `make sharded CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline (Linux/clang):

- **2194** compile+link+runtime passing tests (unchanged from the afternoon
  baseline; the static-member tests were already absent from the suite before
  this PR)
- **149** `_fail` tests failing as expected
- overall result: **SUCCESS**

### Still open Phase 6 items

- ~~Unsized array with pack-expanded initializer keeps pre-expansion size 1.~~ (fixed — see below)
- ~~Aggregate brace-init of a struct with pack-expanded initializer parse
  failure at the call site.~~ (fixed — see below)

## Phase 6 continued: pack-expanded brace initializers for unsized arrays and aggregate structs (2026-04-23)

Closed both remaining Phase 6 items in `docs/KNOWN_ISSUES.md`.  The file is now
empty (all previously-documented issues have been resolved).

### Root causes

1. **Unsized array `int arr[] = {args...}` kept pre-expansion size 1.**
   `Parser::inferUnsizedArraySizeFromInitializer` runs at parse time and sets
   the outer dimension from `init_list.initializers().size()`.  Before template
   substitution, the list contains a single `PackExpansionExprNode`, so the
   dimension was fixed at 1.  Although `substituteTemplateParameters` for
   `InitializerListNode` correctly expanded the pack into N substituted
   elements, the `DeclarationNode`'s `TypeSpecifierNode` still carried the
   stale `[1]` dimension, which `IrGenerator_Stmt_Decl.cpp` then preferred over
   the init-list size.

2. **Aggregate brace-init `Triple t = {args...};` rejected at parse time.**
   The positional aggregate-init branch of `parse_brace_initializer` (in
   `src/Parser_Statements.cpp`) called `parse_expression(2, Normal)` without
   checking for a trailing `...`, unlike `parse_brace_initializer_clause_list`
   (the array-path) which already wrapped the element in a
   `PackExpansionExprNode` when `...` followed.  The parser then either tried
   to parse `...` as the next positional initializer (failing) or emitted
   "Failed to parse initializer expression".

### Fixes

- `src/Parser_Templates_Substitution.cpp / VariableDeclarationNode branch`
  — after substituting the declaration and initializer, when the declaration
  is an unsized array and the substituted initializer is an
  `InitializerListNode`, emplace a fresh `TypeSpecifierNode` copy (to avoid
  mutating the pattern's shared spec for non-template element types like
  `int`) and re-run `inferUnsizedArraySizeFromInitializer` against the
  expanded initializer.  The `DeclarationNode::set_type_node` setter is used
  to swap in the updated spec.
- `src/Parser_Statements.cpp / parse_brace_initializer positional branch`
  — after `parse_expression`, detect a trailing `...` and wrap the element in
  a `PackExpansionExprNode`, mirroring the existing logic in
  `parse_brace_initializer_clause_list` and the array-element branch of
  `parse_brace_initializer`.  When a pack expansion is seen, advance
  `member_index` past the last member to suppress the parse-time
  "too many initializers" check (pack size is only known after substitution;
  downstream sema/IR-gen validates the expanded element count against the
  struct's member count).

Regression tests:
- `tests/test_unsized_array_pack_expansion_ret3.cpp` — exercises
  `int arr[] = {static_cast<int>(args)...}` and asserts
  `sizeof(arr)/sizeof(arr[0]) == 3`.
- `tests/test_aggregate_struct_pack_expansion_ret42.cpp` — exercises
  `Triple t = {static_cast<int>(args)...};` and asserts
  `t.a + t.b + t.c == 42`.

### Validation baseline (2026-04-23 · late evening)

- `make sharded CXX=clang++`
- `bash ./tests/run_all_tests.sh`

Current baseline (Linux/clang):

- **2197** compile+link+runtime passing tests (+2 vs evening baseline from the
  new regression tests above)
- **149** `_fail` tests failing as expected
- overall result: **SUCCESS**

### Phase 6 status

With the two brace-init-pack-expansion issues closed, all items recorded in
`docs/KNOWN_ISSUES.md` as of this PR are resolved and the file is empty.  The
Phase 6 roadmap section above still lists "nested template packs and other
complex mapping shapes that may surface as the test corpus grows" as the
general open-ended follow-up, but there are no concrete reproducers tracked.

## Validation baseline refreshed on 2026-04-23 (Phase 6: nested wrapped pack deduction/materialization)

Validation run after:
- recursive nested dependent-name discovery / extraction in `src/Parser_Templates_Inst_Deduction.cpp`
- recursive nested placeholder materialization in `src/Parser_Expr_QualLookup.cpp`
- member-template pack-parameter materialization alignment in `src/Parser_Templates_Inst_MemberFunc.cpp`
- adding:
  - `tests/test_nested_box_pack_implicit_deduction_ret0.cpp`
  - `tests/test_nested_multi_dep_pack_ret0.cpp`
  - `tests/test_explicit_nested_multi_dep_pack_ret0.cpp`
  - `tests/test_class_template_nested_pack_member_ret0.cpp`

Validation commands:
- `clang++ -std=c++20 -fsyntax-only tests/test_nested_box_pack_implicit_deduction_ret0.cpp tests/test_nested_multi_dep_pack_ret0.cpp tests/test_explicit_nested_multi_dep_pack_ret0.cpp tests/test_class_template_nested_pack_member_ret0.cpp`
- `make sharded CXX=clang++`
- `bash ./tests/run_all_tests.sh test_nested_box_pack_implicit_deduction_ret0.cpp test_nested_multi_dep_pack_ret0.cpp test_explicit_nested_multi_dep_pack_ret0.cpp test_class_template_nested_pack_member_ret0.cpp test_implicit_deduction_box_pack_ret0.cpp test_multi_dep_pack_ret0.cpp test_explicit_multi_dep_pack_ret0.cpp test_class_template_inner_func_pack_fold_ret0.cpp test_class_template_static_member_func_template_ret0.cpp`
- `bash ./tests/run_all_tests.sh`

Current baseline (Linux/clang):
- **2201** compile+link+runtime passing tests (+4 from the nested wrapped-pack regressions)
- **149** `_fail` tests failing as expected
- overall result: **SUCCESS**
