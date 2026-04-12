# Template Instantiation Identity / Materialization Follow-up Plan

**Date:** 2026-04-08  
**Last Updated:** 2026-04-12  
**Context:** Follows the branch fix that made `test_integral_constant_comprehensive_ret100.cpp`, `test_integral_constant_pattern_ret42.cpp`, `test_ratio_less_alias_ret0.cpp`, `test_sfinae_enable_if_ret0.cpp`, and `test_sfinae_same_name_overload_ret0.cpp` pass by preserving dependent non-type template-argument identity in template-instantiation keys.

## Quick start for next agent

### Current baseline (2026-04-12)

- Linux: `make main CXX=clang++` compiles cleanly
- Linux: `bash ./tests/run_all_tests.sh` → 2052 pass, 132 expected-fail
- All key regression tests pass:
  ```bash
  bash ./tests/run_all_tests.sh test_explicit_template_defaulted_param_deduction_ret42.cpp \
    test_namespace_qualified_explicit_template_defaulted_param_deduction_ret42.cpp \
    test_global_namespace_qualified_explicit_template_defaulted_param_deduction_ret42.cpp \
    test_pack_decltype_simple_ret42.cpp \
    test_variadic_template_pack_before_tail_trailing_return_ret0.cpp \
    test_namespaced_pair_swap_sfinae_ret0.cpp
  ```

### Current work in progress: Phase 1 (IN PROGRESS)

**Phase 1 status:** ~70% complete. `NonTypeValueIdentity` carrier introduced, `ValueArgKey` now uses it.

**Next steps for Phase 1:**
- Optional: Update `toHashString()` and `TemplateTypeArgHash` to share hash logic with `NonTypeValueIdentity`
- Optional: Remove redundant fields from `TemplateTypeArg` (larger refactor)

**Ready to start Phase 2:** Yes - Phase 1 core work is complete. Phase 2 can begin in parallel.

### Choose your next task

**Option A: Continue Phase 1 (refine non-type value identity)**
- Update hash logic to use shared helpers
- Consider removing redundant fields from `TemplateTypeArg`
- Low priority - the core identity model is already unified

**Option B: Start Phase 2 (centralize alias-template materialization)**
- Add shared helper for alias-template resolution
- Consolidate top-level `using`, struct-local `using`, and type-specifier alias handling
- Key files: `src/Parser_Decl_TopLevel.cpp`, `src/Parser_Decl_TypedefUsing.cpp`, `src/Parser_TypeSpecifiers.cpp`

**Option C: Continue Phase 6 (pack-aware explicit deduction)**
- Design a pack-aware mapping helper in `buildDeductionMapFromCallArgs(...)`
- Currently, pack-bearing signatures fall back to older positional deduction
- Key files: `src/Parser_Templates_Inst_Deduction.cpp:608-881`, `src/Parser.h:865-869`

**Option D: Fix bugs in `docs/KNOWN_ISSUES.md`**
- Currently tracked: premature `layout_is_complete` during anonymous union processing
- Low priority, no user-facing issues currently

**Note:** The "early instantiation without arg_types" gap was investigated on 2026-04-12 and found to NOT be a bug. See detailed notes below.

### Important invariants to preserve

1. **Non-pack signatures**: Use name-based pre-deduction map first, then defaults, then overload mismatch
2. **Pack-bearing signatures**: Use existing positional fallback only
3. **Always validate** after changes:
   - Run the key regression tests listed above
   - Run the full test suite

---

## Next agent starting point (detailed)

### Do this next

1. Keep Phase 6 narrow: the remaining gap is still pack-bearing explicit
   deduction. `buildDeductionMapFromCallArgs(...)` now reaches the qualified
   explicit-call sites for non-pack signatures too, but template-parameter packs
   and function-parameter packs must stay on the older positional fallback until
   there is an explicit pack-aware mapping contract.
2. If you touch `try_instantiate_template_explicit(...)` again, preserve the
   current split:
   - non-pack explicit deduction: name-based pre-deduction map first, then
     defaults, then overload mismatch
   - pack-bearing explicit deduction: existing positional fallback only
3. Before widening Phase 6, reproduce both sides of the split:
   - `test_namespace_qualified_explicit_template_defaulted_param_deduction_ret42.cpp`
   - `test_global_namespace_qualified_explicit_template_defaulted_param_deduction_ret42.cpp`
   - `test_explicit_template_defaulted_param_deduction_ret42.cpp`
   - `test_pack_decltype_simple_ret42.cpp`
   - `test_variadic_template_pack_before_tail_trailing_return_ret0.cpp`
   - `test_namespaced_pair_swap_sfinae_ret0.cpp`
4. If you continue Phase 6 after that, the next worthwhile slice is designing a
   pack-aware mapping helper rather than widening the current name-based remap
   in place.
5. **~~Known gap — early instantiation without arg_types~~ (INVESTIGATED: NOT A BUG)**:
   The early `try_instantiate_template_explicit(name, *template_args)` call at
   `src/Parser_Expr_PrimaryExpr.cpp:2405` does fire and can succeed for function
   templates, BUT it does not cause issues because:
   - The early instantiation uses `explicit_args + defaults` as template args
   - The later instantiation uses `explicit_args + deduced_from_call_args`
   - These produce DIFFERENT instantiation keys (e.g., `func<T, int>` vs `func<T, Marker>`)
   - The later path creates a separate instantiation with the correctly deduced args
   - The call expression uses the later instantiation, not the early one
   This was verified by debug tracing on 2026-04-12. The warning message at line 2411
   ("Parsed template arguments but instantiation failed") appears but is benign.

### Latest completed slice

  - threaded explicit call-argument types through the remaining qualified
    explicit-template call paths in:
    - `src/Parser_Expr_PrimaryExpr.cpp`
    - `src/Parser_Expr_PostfixCalls.cpp`
  - kept the non-call qualified-id/template-reference paths unchanged because
    they still do not have call-argument types available at that stage
  - added:
    - `tests/test_namespace_qualified_explicit_template_defaulted_param_deduction_ret42.cpp`
    - `tests/test_global_namespace_qualified_explicit_template_defaulted_param_deduction_ret42.cpp`
  - validation after this slice:
    - `make main CXX=clang++`
    - `bash ./tests/run_all_tests.sh test_explicit_template_defaulted_param_deduction_ret42.cpp test_namespace_qualified_explicit_template_defaulted_param_deduction_ret42.cpp test_global_namespace_qualified_explicit_template_defaulted_param_deduction_ret42.cpp test_pack_decltype_simple_ret42.cpp test_variadic_template_pack_before_tail_trailing_return_ret0.cpp test_namespaced_pair_swap_sfinae_ret0.cpp`
    - `bash ./tests/run_all_tests.sh`
    - 2045 pass, 132 expected-fail

### Previous completed slice

  - tightened `try_instantiate_template_explicit(...)` so the shared
    `buildDeductionMapFromCallArgs(...)` helper is the only explicit-deduction
    path for signatures without template/function parameter packs
  - kept the older positional explicit-deduction fallback only for pack-bearing
    signatures, matching the Phase 6 guardrail that broader remapping needs an
    explicit pack-aware contract first
  - clarified the shared helper contract in `src/Parser.h` so future follow-up
    work does not accidentally reuse the non-pack path for pack-bearing
    signatures
  - validation after this slice:
    - `make main CXX=clang++`
    - `bash ./tests/run_all_tests.sh test_explicit_template_defaulted_param_deduction_ret42.cpp test_pack_decltype_simple_ret42.cpp test_variadic_template_pack_before_tail_trailing_return_ret0.cpp test_namespaced_pair_swap_sfinae_ret0.cpp`
    - `bash ./tests/run_all_tests.sh`
    - 2038 pass, 132 expected-fail

### Previous next-agent notes

1. Keep `materializePrimaryTemplateOwnerForLookup(...)` itself
   registration-free. The new
   `materializePrimaryTemplateOwnerForConstructorLookup(...)` wrapper in
   `src/Parser_Expr_PrimaryExpr.cpp` stays constructor-style only. The older
   qualified-id functional-cast fix now reuses it locally, but only for
   non-dependent `ns::Template<Args>()` temporaries.
2. If you extend the older qualified-id path further, keep the new branch local
   to constructor-style parsing and continue skipping dependent template-arg
   cases until there is an explicit placeholder/deferred contract for
   `ns::Template<T>()`.
3. Reproduce the qualified-id constructor cluster before touching that code:
   - `test_namespace_template_default_functional_cast_ret42.cpp`
   - `test_namespace_alias_template_default_functional_cast_ret42.cpp`
   - `test_nested_namespace_template_default_functional_cast_ret42.cpp`
   - `test_namespaced_pair_swap_sfinae_ret0.cpp`
4. Continue Phase 6 from the new stable baseline: keep the name-based
   `buildDeductionMapFromCallArgs(...)` path for explicit deduction only when
   there is no template-parameter pack and no function-parameter pack. Any
   broader remap for pack-bearing signatures needs an explicit pack-aware
   mapping contract first.

### Previous completed slice

  - extracted the duplicated template functional-style cast parsing/building
    into `Parser::parseMaterializedTemplateFunctionalCast(...)` in:
    - `src/Parser.h`
    - `src/Parser_Expr_PrimaryExpr.cpp`
  - fixed the older qualified-id branch in
    `src/Parser_Expr_PrimaryExpr.cpp` so non-dependent
    `ns::Template<Args>()` expressions no longer fall through into the generic
    qualified-id call path; when class/alias owner materialization succeeds,
    the parser now builds a constructor-style temporary directly
  - deliberately kept that older qualified-id fix local and narrow:
    - still uses `materializePrimaryTemplateOwnerForConstructorLookup(...)`
      only from the constructor-style branch
    - still skips dependent explicit template arguments
    - still leaves `materializePrimaryTemplateOwnerForLookup(...)`
      registration-free
  - added:
    - `tests/test_namespace_template_default_functional_cast_ret42.cpp`
    - `tests/test_namespace_alias_template_default_functional_cast_ret42.cpp`
    - `tests/test_nested_namespace_template_default_functional_cast_ret42.cpp`
  - validation after this slice:
    - `.\build_flashcpp.bat`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1 test_template_default_functional_cast_ret42.cpp test_late_member_body_class_template_functional_style_ret42.cpp test_namespaced_pair_swap_sfinae_ret0.cpp`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1 test_namespace_template_default_functional_cast_ret42.cpp test_template_default_functional_cast_ret42.cpp test_namespace_template_default_member_ret42.cpp test_nested_namespace_template_default_member_ret42.cpp test_namespaced_pair_swap_sfinae_ret0.cpp`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1 test_namespace_template_default_functional_cast_ret42.cpp test_namespace_alias_template_default_functional_cast_ret42.cpp test_nested_namespace_template_default_functional_cast_ret42.cpp test_template_default_functional_cast_ret42.cpp test_namespaced_pair_swap_sfinae_ret0.cpp`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`
    - 2061 pass, 131 expected-fail

### Older completed slices

  - extracted shared explicit/implicit pre-deduction into
    `Parser::buildDeductionMapFromCallArgs(...)` in:
    - `src/Parser.h`
    - `src/Parser_Templates_Inst_Deduction.cpp`
  - switched `try_instantiate_template_explicit(...)` to consult that
    name-based map before defaults for the stable non-pack case, fixing the
    `deduced_call_arg_index` bug where defaulted-but-deducible parameters could
    incorrectly fall back to their defaults
  - added:
    - `tests/test_explicit_template_defaulted_param_deduction_ret42.cpp`
  - narrowed the new explicit-deduction remap back to the stable cases only:
    signatures with template/function parameter packs now stay on the older
    positional fallback until a pack-aware mapping is designed, which restores:
    - `tests/test_variadic_template_pack_before_tail_trailing_return_ret0.cpp`
  - extracted the parser-owned constructor-style registration contract into
    `materializePrimaryTemplateOwnerForConstructorLookup(...)` and switched only
    the three already-safe constructor-style exits in
    `src/Parser_Expr_PrimaryExpr.cpp` to use it
  - important non-merged finding:
    - `ns::Template<Args>()` still flows through the older qualified-id call
      path rather than the newer constructor-style exits; a scratch regression
      in that shape still bottoms out in codegen with
      `struct type info not found for type_index=0`
  - validation after this slice:
    - `.\build_flashcpp.bat`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1 test_explicit_template_defaulted_param_deduction_ret42.cpp test_namespaced_pair_swap_sfinae_ret0.cpp`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1 test_variadic_template_pack_before_tail_trailing_return_ret0.cpp test_explicit_template_defaulted_param_deduction_ret42.cpp test_namespaced_pair_swap_sfinae_ret0.cpp`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1 test_template_default_functional_cast_ret42.cpp test_namespace_template_default_member_ret42.cpp test_nested_namespace_template_default_member_ret42.cpp test_late_member_body_class_template_functional_style_ret42.cpp test_method_on_temporary_ret0.cpp test_namespaced_pair_swap_sfinae_ret0.cpp test_explicit_template_defaulted_param_deduction_ret42.cpp`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`
    - 2058 pass, 131 expected-fail

  - added an explicit parser-owned
    `registerAndNormalizeLateMaterializedTopLevelNode(...)` step to the
    namespace-qualified brace-init path in
    `src/Parser_Expr_PrimaryExpr.cpp` right after
    `materializePrimaryTemplateOwnerForLookup(...)`, so the
    `ns::Template<Args>{...}` branch normalizes freshly materialized class
    templates before building its `ConstructorCallNode`
  - deliberately left the older `qual_id`-based owner-materialization exit in
    `src/Parser_Expr_PrimaryExpr.cpp` registration-free after confirming that
    adding the same step there still regresses:
    - `test_namespaced_pair_swap_sfinae_ret0.cpp`
  - validation after this slice:
    - `make main CXX=clang++`
    - `bash ./tests/run_all_tests.sh test_namespaced_pair_swap_sfinae_ret0.cpp test_template_static_member_initializer_nested_constexpr_member_call_ret42.cpp test_method_on_temporary_ret0.cpp test_late_member_body_class_template_functional_style_ret42.cpp test_template_default_functional_cast_ret42.cpp test_namespace_template_default_member_ret42.cpp test_nested_namespace_template_default_member_ret42.cpp`
    - `bash ./tests/run_all_tests.sh`
    - 2027 pass, 131 expected-fail
  - switched the remaining parser-owned single-node class-template
    materialization exits that were still using bare
    `registerLateMaterializedTopLevelNode(...)` in:
    - `src/Parser_Statements.cpp`
    - `src/Parser_Templates_Class.cpp`
    - `src/Parser_Templates_Inst_ClassTemplate.cpp`
    - `src/Parser_Templates_Inst_Substitution.cpp`
    - `src/Parser.h`
  - kept `materializePrimaryTemplateOwnerForLookup(...)` registration-free after
    confirming that moving registration/normalization into the helper itself
    regressed:
    - `test_namespaced_pair_swap_sfinae_ret0.cpp`
    - `test_template_static_member_initializer_nested_constexpr_member_call_ret42.cpp`
  - moved the parser-owned late-materialization step in
    `src/Parser_Expr_PrimaryExpr.cpp` to the two functional-style cast exits
    instead, using `registerAndNormalizeLateMaterializedTopLevelNode(...)`
    directly on the returned `instantiated_struct_node`
  - validation after this slice:
    - `make main CXX=clang++`
    - `bash ./tests/run_all_tests.sh test_namespaced_pair_swap_sfinae_ret0.cpp test_template_static_member_initializer_nested_constexpr_member_call_ret42.cpp test_method_on_temporary_ret0.cpp test_late_member_body_class_template_functional_style_ret42.cpp test_template_default_functional_cast_ret42.cpp test_template_dependent_placeholder_base_ret0.cpp test_extern_template_ret0.cpp test_extern_template_ns_qualified_ret0.cpp test_explicit_template_nontype_ret5.cpp test_qualified_base_nested_member_alias_ret42.cpp test_qualified_base_full_spec_alias_chain_ret42.cpp test_identifier_binding_template_member_outofline_implicit_member_ret42.cpp`
    - `bash ./tests/run_all_tests.sh`
    - 2025 pass, 131 expected-fail
  - added `Parser::registerAndNormalizeLateMaterializedTopLevelNode(...)` and
    `Parser::registerAndNormalizeLateMaterializedTopLevelNodeFront(...)` in
    `src/Parser.h` so late AST-root registration can explicitly drain pending
    semantic roots in the same parser-owned step
  - switched the targeted lazy/member-function/deduction/type-specifier exits
    called out above to the new helper in:
    - `src/Parser_Templates_Lazy.cpp`
    - `src/Parser_Templates_Inst_MemberFunc.cpp`
    - `src/Parser_Templates_Inst_Deduction.cpp`
    - `src/Parser_Templates_Inst_Substitution.cpp`
    - `src/Parser_TypeSpecifiers.cpp`
  - kept the full-specialization member replay in
    `src/Parser_Templates_Inst_Substitution.cpp` batched, but added one shared
    `normalizePendingSemanticRootsIfAvailable()` drain after the constructor /
    destructor / method registration loop so the queue is normalized once per
    specialization replay instead of depending on outer callers
  - validation after this slice:
    - `make main CXX=clang++`
    - `bash ./tests/run_all_tests.sh member_func_template_call_ret3.cpp template_member_access_ret42.cpp test_template_default_member_lookup_ret42.cpp test_identifier_binding_template_member_outofline_implicit_member_ret42.cpp test_template_spec_outofline_default_arg_ret42.cpp test_template_lazy_static_member_implicit_this_fail.cpp`
    - `bash ./tests/run_all_tests.sh`
    - 2006 pass, 128 expected-fail
  - replaced the last manual class-template constructor materialization branch
    in `src/ExpressionSubstitutor.cpp` with
    `materializeTemplateInstantiationForLookup(...)`, so the substitution layer
    no longer keeps its own `try_instantiate_class_template(...)` +
    type-map-recovery path for explicit template constructor calls
  - switched that constructor path to use the resolved `TypeInfo`’s registered
    type index/category instead of raw `type_index_` access
  - removed the dead `ExpressionSubstitutor::ensureTemplateInstantiated(...)`
    declaration/definition because it had no remaining call sites after the
    shared helper migration
  - Validation after this slice:
    - `make main CXX=clang++`
    - `bash ./tests/run_all_tests.sh test_template_ctor_ret0.cpp test_partial_spec_template_ctor_ret0.cpp test_ool_template_ctor_ret5.cpp test_ool_template_ctor_brace_init_ret10.cpp test_decltype_base_with_substitution_ret42.cpp`
    - `bash ./tests/run_all_tests.sh`
    - 2004 pass, 128 expected-fail
  - removed the last two unqualified `Template<Args>::member` fallback branches
    in `src/Parser_Expr_PrimaryExpr.cpp`, so the primary-expression parser no
    longer rebuilds instantiated owners by hand after the shared helper runs
  - switched out-of-line `ClassName<Args>::member` owner recovery in
    `src/Parser_Decl_FunctionOrVar.cpp` to
    `materializePrimaryTemplateOwnerForLookup(...)` instead of open-coding
    `get_instantiated_class_name(...)` plus namespace/type-map fallback
  - added `tests/test_template_spec_outofline_default_arg_ret42.cpp` to cover
    an out-of-line specialization that only binds when owner recovery completes
    the defaulted class-template argument
  - fixed the previously tracked template-object construction regression by
    constexpr-folding substituted non-static default member initializers during
    top-level class-template instantiation, so `sizeof(U)`-driven initializers
    survive both named-object and temporary construction
  - replaced the two remaining namespace-qualified
    `src/Parser_Expr_PrimaryExpr.cpp` replay branches with the shared
    parser-owned `materializePrimaryTemplateOwnerForLookup(...)` helper so both
    the direct `ns::Template<Args>::member` path and the older explicit
    qualified-id flow stop rebuilding instantiated owners by hand
  - added:
    - `tests/test_template_object_default_member_init_sizeof_ret84.cpp`
    - `tests/test_namespace_template_default_member_ret42.cpp`
    - `tests/test_nested_namespace_template_default_member_ret42.cpp`
    - `tests/test_template_spec_outofline_default_arg_ret42.cpp`
  - Validation after this slice:
    - `.\build_flashcpp.bat`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1 test_template_spec_outofline_default_arg_ret42.cpp test_template_spec_outofline_ret42.cpp test_identifier_binding_template_member_outofline_implicit_member_ret42.cpp test_template_member_outofline_mixed_order_bindings_ret42.cpp`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1 test_template_object_default_member_init_sizeof_ret84.cpp test_template_nested_default_member_init_sizeof_ret42.cpp test_template_nested_default_member_init_nttp_ret42.cpp`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1 test_namespace_template_default_member_ret42.cpp test_namespace_alias_template_default_member_ret42.cpp test_template_alias_member_qualifier_compose_ret0.cpp`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1 test_namespace_template_default_member_ret42.cpp test_nested_namespace_template_default_member_ret42.cpp`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`
    - 2008 pass, 125 expected-fail
  - replaced three remaining `src/Parser_Expr_PrimaryExpr.cpp` owner/name
    recovery branches with the shared parser-owned
    `materializePrimaryTemplateOwnerForLookup(...)` helper so explicit
    `Template<Args>::member` parsing (qualified and unqualified) and
    template functional-style casts no longer hand-fill default arguments and
    rebuild instantiated owner names independently
  - added:
    - `tests/test_namespace_alias_template_default_member_ret42.cpp`
    - `tests/test_template_default_member_lookup_ret42.cpp`
    - `tests/test_template_default_functional_cast_ret42.cpp`
  - Validation after this slice:
    - `.\build_flashcpp.bat`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1 test_namespace_alias_template_default_member_ret42.cpp test_template_alias_member_qualifier_compose_ret0.cpp test_template_default_qualified_arg_order_ret42.cpp`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1 test_template_default_member_lookup_ret42.cpp test_namespace_alias_template_default_member_ret42.cpp test_template_alias_member_qualifier_compose_ret0.cpp`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1 test_template_default_functional_cast_ret42.cpp test_template_default_member_lookup_ret42.cpp test_namespace_alias_template_default_member_ret42.cpp`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`
    - 2002 pass, 125 expected-fail
  - reran the previously reported alias/array crash cluster and confirmed it is
    no longer live on the branch; the current materialization baseline already
    compiles and runs those regressions cleanly
  - extracted shared late `TypeInfo::templateArgs()` rebinding in
    `src/ExpressionSubstitutor.cpp` so qualified-identifier replay and late
    type substitution now use the same dependent-name / surviving-type-name
    substitution path instead of maintaining two copies
  - switched `Template<Args>::member(...)` owner resolution in
    `src/Parser_Expr_BinaryPrecedence.cpp` to
    `materializeTemplateInstantiationForLookup(...)` instead of the old
    `try_instantiate_class_template(...)` + `get_instantiated_class_name(...)`
    pair
  - removed the redundant eager class-template instantiation in
    `src/Parser_Expr_PostfixCalls.cpp` before
    `parse_template_brace_initialization(...)`, leaving the shared brace-init
    helper as the only owner-materialization path there
  - Validation after this slice:
    - `.\build_flashcpp.bat`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1 test_template_alias_funcptr_ret0.cpp test_template_alias_member_qualifier_compose_ret0.cpp test_template_type_alias_array_member_brace_init_ret0.cpp test_template_type_alias_array_member_extra_outer_args_ret0.cpp test_template_type_alias_array_member_substring_name_ret0.cpp`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1 test_struct_local_alias_static_init_ret0.cpp test_alias_template_nested_member_value_ret42.cpp test_nested_template_instantiation_ret42.cpp test_member_template_default_value_substitution_ret0.cpp`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1 member_func_template_call_ret3.cpp template_member_access_ret42.cpp test_phase3_decltype_context_ret42.cpp test_explicit_condition_ret42.cpp`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1 test_alias_template_brace_init_ret42.cpp test_template_brace_init_ret42.cpp test_template_brace_init_userdefined_ret3.cpp template_template_brace_init_ret0.cpp`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`
    - 2000 pass, 125 expected-fail
  - fixed the alias-metadata regression in the shared concrete-owner/member
    resolver by preferring an exact published `InstantiatedOwner::member` entry
    before walking a chained member lookup, which preserves array extents,
    function signatures, and qualifier-composition aliases
  - collapsed the ordinary placeholder and template-template placeholder paths
    in `substitute_template_parameter(...)` onto the same
    parser-owned concrete-owner/member-chain materialization helper instead of
    open-coding instantiation, registry fallback, instantiated-name recovery,
    and chain replay twice
  - centralized the remaining placeholder-arg fallback for cases that still
    encode a dependent template parameter as a struct-typed placeholder whose
    type name matches the parameter name, so that rebinding logic now lives in
    one local helper instead of a one-off loop in the template-template branch
  - switched `src/ExpressionSubstitutor.cpp` explicit member-template owner
    recovery to `materializeTemplateInstantiationForLookup(...)` instead of
    calling `try_instantiate_class_template(...)` plus
    `get_instantiated_class_name(...)` directly
  - Validation after this slice:
    - `.\build_flashcpp.bat`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1 test_template_template_alias_chain_ret42.cpp test_template_template_full_spec_alias_chain_ret42.cpp test_alias_template_brace_init_ret42.cpp test_template_alias_funcptr_ret0.cpp test_template_alias_member_qualifier_compose_ret0.cpp test_template_type_alias_array_member_brace_init_ret0.cpp test_template_type_alias_array_member_extra_outer_args_ret0.cpp test_template_type_alias_array_member_substring_name_ret0.cpp`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1 member_func_template_call_ret3.cpp test_nested_template_instantiation_ret42.cpp test_identifier_binding_template_member_outofline_implicit_member_ret42.cpp test_member_function_template_in_partial_spec_ret0.cpp test_member_template_func_in_specialization_ret0.cpp test_member_template_default_value_substitution_ret0.cpp`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`
    - 2000 pass, 125 expected-fail
  - taught unqualified template brace initialization to recognize alias
    templates as first-class materialization candidates instead of falling
    through with `Missing semicolon` at `Alias<T>{...}`
  - switched `parse_template_brace_initialization(...)` onto
    `materializeTemplateInstantiationForLookup(...)` so alias templates,
    registry fallback, and resolved `TypeInfo` lookup use the same parser-owned
    path as the other materialization hardening work
  - extended dependent template-parameter placeholders in
    `src/Parser_TypeSpecifiers.cpp` to preserve chained member access like
    `TT<T>::type::type` instead of truncating after the first `::type`
  - taught `substitute_template_parameter(...)` to replay those preserved
    member chains against the concrete instantiation, including
    template-template parameters
  - replaced the remaining incomplete-base fallback in
    `validate_and_add_base_class(...)` with
    `materializeTemplateInstantiationForLookup(...)` so base lookup no longer
    hand-rolls another instantiation / lookup path
  - added:
    - `tests/test_alias_template_brace_init_ret42.cpp`
    - `tests/test_template_template_alias_chain_ret42.cpp`
    - `tests/test_template_template_full_spec_alias_chain_ret42.cpp`
  - Validation after this slice:
    - `.\build_flashcpp.bat`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1 test_alias_template_brace_init_ret42.cpp test_template_template_alias_chain_ret42.cpp test_template_template_full_spec_alias_chain_ret42.cpp test_qualified_base_nested_member_alias_ret42.cpp test_qualified_base_full_spec_alias_chain_ret42.cpp`
  - taught base-class post-template parsing / deferral to keep the full
    member-type chain after template arguments (for patterns like
    `Base<T>::type::type`) instead of only remembering one trailing `::member`
  - updated immediate and deferred base-class resolution to walk those chained
    member-type suffixes through one parser-owned helper in
    `src/Parser_Expr_QualLookup.cpp`, preserving instantiated-owner lookup
    names while the chain is resolved
  - refreshed exact/full-specialization alias publication so sibling aliases
    like `type = selected;` can bind to the concrete
    `InstantiatedOwner::selected` entry instead of keeping a stale
    declaration-site alias target
  - added:
    - `tests/test_qualified_base_nested_member_alias_ret42.cpp`
    - `tests/test_qualified_base_full_spec_alias_chain_ret42.cpp`
  - Validation after this slice:
    - `.\build_flashcpp.bat`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`
    - 1996 pass, 125 expected-fail
  - extracted the deduction-time `materialize_placeholder_args` logic from
    `src/Parser_Templates_Inst_Deduction.cpp` into the reusable
    `Parser::materializePlaceholderTemplateArgs(...)` helper in `src/Parser.h`
    so dependent non-type placeholder materialization now lives behind one
    parser-owned utility instead of a 100+ line local lambda
  - added `Parser::materializeInstantiatedMemberAliasTarget(...)` in
    `src/Parser_Templates_Inst_Substitution.cpp` / `src/Parser.h` and reused it
    from the primary, partial-specialization, and full-specialization alias
    registration paths
  - updated the full-specialization alias publication path to refresh existing
    qualified alias entries in place instead of silently keeping stale
    placeholder-backed registrations
  - Validation after this slice:
    - `.\build_flashcpp.bat`
    - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`
    - 1994 pass, 125 expected-fail
  - added `Parser::materializeTemplateInstantiationForLookup` in
    `src/Parser_Templates_Inst_Substitution.cpp` / `src/Parser.h` so parser-owned
    materialization now covers alias templates, ordinary template instantiations,
    registry fallback, and pending-sema normalization behind one helper
  - switched the remaining `ExpressionSubstitutor.cpp` template-name recovery sites
    to that helper instead of open-coded
    `instantiate_and_register_base_template(...)` /
    `get_instantiated_class_name(...)` fallback logic
  - hardened instantiated class-template type-alias registration in
    `src/Parser_Templates_Inst_ClassTemplate.cpp` so dependent
    `Base$placeholder::member` alias targets are materialized against the current
    concrete template arguments and pinned under qualified owner names
  - extended the struct-local alias consumer path in `src/ExpressionSubstitutor.cpp`
    so nested alias uses like `holder<B>::selected::value` rematerialize the
    deferred alias target before constexpr/static-member normalization
  - added `tests/test_alias_template_nested_member_value_ret42.cpp` to cover nested
    deferred alias/member consumption inside an instantiated class template
  - introduced `Parser::AliasTemplateMaterializationResult` in
    `src/Parser_Templates_Inst_Substitution.cpp` / `src/Parser.h` so deferred alias-template
    materialization keeps both the instantiated base name and any resolved concrete `TypeInfo`
  - switched the deferred alias path in `src/Parser_TypeSpecifiers.cpp` to use that structured
    helper for template-parsing and implicit/explicit member-alias cases instead of the older
    open-coded alias-chain/class-instantiation fallback
  - consolidated deferred alias member lookup / placeholder creation in
    `src/Parser_TypeSpecifiers.cpp` onto one local finalization path and normalized pending
    semantic roots before consuming the materialized result
  - added `tests/test_alias_template_deferred_return_ret0.cpp` to cover deferred alias-template
    materialization in a template return type (`choose_value_t<B>`)
- Validation after this slice: `make main CXX=clang++` and `bash tests/run_all_tests.sh` —
  1949 pass, 0 fail.
### Earlier completed work on this branch

- Completed in this slice:
  - reused `resolveAliasTemplateInstantiation(...)` from `src/Parser_Templates_Inst_Substitution.cpp`
    in `src/Parser_TypeSpecifiers.cpp` for ordinary deferred alias-template type parsing when we
    are outside template instantiation and the alias target is not an implicit `::member` alias
  - collapsed the duplicated ordinary alias-template member-resolution tail in
    `src/Parser_TypeSpecifiers.cpp` onto one local finalization path
  - kept the legacy deferred fallback for template-instantiation/member-alias cases so
    `enable_if_t`/SFINAE placeholder behavior still matches the pre-refactor path
  - added `tests/test_alias_template_chain_type_specifier_ret42.cpp`
  - added `tests/test_alias_template_member_type_type_specifier_ret42.cpp`
  - fixed the struct-local type alias static-initializer bug by teaching `ExpressionSubstitutor`
    to resolve `QualifiedIdentifierNode` namespaces like `constant_type` through the current
    instantiated owner type before falling back to template-name parsing
  - threaded the current instantiated owner type name into the static-member substitution paths in
    `Parser_Templates_Inst_ClassTemplate.cpp` and `Parser_Templates_Lazy.cpp`
  - added `tests/test_struct_local_alias_static_init_ret0.cpp`
  - Fixed hash drift in `ValueArgKey::hash()` (`src/TemplateTypes.h:200`): changed
    `std::hash<uint32_t>{}(dependent_name.handle)` to `std::hash<StringHandle>{}(dependent_name)`
    so both `TemplateTypeArg::hash()` and `ValueArgKey::hash()` use the same hash strategy for
    dependent names (Phase 1 canonicalization).
  - added `tests/test_toplevel_alias_chain_nontype_ret42.cpp` — covers top-level `using` alias
    chain with bool/int non-type template arguments (`bool_constant<true/false>` accessed through
    `true_type`/`false_type` aliases and `cond_val<B>`).
  - removed the fixed struct-local alias static-init issue from `docs/KNOWN_ISSUES.md`
- Validation: `make main CXX=clang++` and `bash tests/run_all_tests.sh` — 1948 pass, 0 fail.
- Recommended next steps (priority order):
  1. Extend the shared alias-template helper path to template-instantiation and implicit-member
     deferred aliases (`enable_if_t`, `typename alias<...>::type`) without regressing the existing
     placeholder/member lookup semantics.
  2. Collapse the remaining alias/class instantiation fallback logic in `ExpressionSubstitutor.cpp`
     onto the same structured helper result instead of open-coded name recovery.
  3. Continue Phase 1: extract `materialize_placeholder_args` lambda
     (`src/Parser_Templates_Inst_Deduction.cpp:2059-2178`) to a `Parser` member function (template
     on `ParamContainer`/`ArgContainer` like the existing `resolveBaseInitializerNameForTemplateArgs`).

---

## Executive summary

The immediate regression is fixed, but the compiler still spreads template-instantiation identity and alias-template materialization across too many parser-owned paths.

The main architectural problem is no longer just "dependent `B` collided with concrete `false`". The broader issue is that the same logical fact is represented in several partially overlapping ways:

- `TemplateTypeArg` stores value/template/dependency state directly in `src\TemplateRegistry_Types.h:170-183`
- `TemplateInstantiationKey` stores a second value-identity model in `src\TemplateTypes.h:185-240`
- alias-template materialization is reimplemented in several parser entry points instead of one authoritative helper
- late materialized roots are normalized through an ad-hoc queue/flush contract rather than a single explicit work model

The next cleanup should therefore happen in this order:

1. **Canonicalize non-type template-argument identity**
2. **Centralize alias-template materialization and alias-chain resolution**
3. **Make late materialization and pending-sema normalization one explicit contract**
4. **Replace placeholder-name heuristics with explicit unresolved-dependent states**

This is intentionally narrower than a full "move all template instantiation into sema" rewrite. It is the smallest architectural slice that removes the class of bug we just fixed while improving the parser/sema boundary for future work.

---

## Current branch status

### What is already better

- Dependent non-type arguments now keep distinct identity in `TemplateInstantiationKey`, so declaration-time placeholders do not collide with concrete values like `false`.
- Top-level and non-top-level alias registrations now materialize alias-template-backed types more consistently.
- SFINAE return-type viability is stricter when a dependent `::type` placeholder survives alias resolution.
- The original five metaprogramming regressions are passing again.

### What is still structurally weak

#### 1. Non-type template-argument identity is still split across two layers

`TemplateTypeArg` still mixes these concerns directly:

- value-vs-type-vs-template-template
- concrete value storage
- dependency tagging
- dependent-name storage

Relevant code:

- `src\TemplateRegistry_Types.h:170-183`
- `src\TemplateRegistry_Types.h:348-365`
- `src\TemplateRegistry_Types.h:483-565`

Then `TemplateInstantiationKey` re-encodes part of the same concept separately via `ValueArgKey`:

- `src\TemplateTypes.h:185-240`
- `src\TemplateTypes.h:245-265`
- `src\TemplateRegistry_Types.h:699-721`

That fixed the immediate collision, but it still means identity is partly owned by `TemplateTypeArg` and partly by the key-building layer.

#### 2. Alias-template materialization is duplicated

The same broad job shows up in multiple places:

- parse a raw alias-template-id use
- substitute alias parameters into target template arguments
- evaluate non-type argument expressions when concrete
- follow alias chains
- instantiate the final class template
- register late materialized structs
- rewrite the resulting `TypeSpecifierNode`

Relevant code:

- `src\Parser_Decl_TopLevel.cpp:827-1071`
- `src\Parser_Decl_TypedefUsing.cpp:410-434`
- `src\Parser_TypeSpecifiers.cpp:995-1235`
- `src\Parser_Templates_Inst_Substitution.cpp:19-180`

This is the clearest place where one bug can require touching several parser files.

#### 3. Late materialization is still parser-driven and normalized opportunistically

There is already useful infrastructure:

- pending semantic roots in `src\Parser.h:976-993`
- parser-to-sema bridge in `src\Parser_Core.cpp:419-423`
- IR-side bridge in `src\IrGenerator_Helpers.cpp:5-11`

But the actual call pattern is still scattered:

- template/class instantiation sites call `registerLateMaterializedTopLevelNode(...)` from many parser files
- some sites also call `normalizePendingSemanticRootsIfAvailable()`
- constexpr lazy-member lookup can instantiate members on demand and then flush pending roots immediately (`src\ConstExprEvaluator_Members.cpp:1319-1326`)

That works, but it is still too easy for a new instantiation path to forget one part of the contract.

#### 4. Some viability checks still infer semantics from names instead of explicit state

The current SFINAE guard in `src\Parser_Templates_Inst_Deduction.cpp:2175-2190` is an improvement, but it still detects one important unresolved case through:

- incomplete-instantiation state
- plus a string-level `::` name heuristic

That is a useful stopgap, not a final representation.

---

## Target invariants

After this follow-up work:

1. A non-type template argument has **one canonical identity model** from parser capture through registry lookup and instantiated-name generation.
2. Alias-template uses are materialized through **one authoritative helper**, not hand-reimplemented at each parser entry point.
3. Any late-materialized AST root that becomes visible to sema, constexpr, or codegen goes through **one explicit registration + normalization path**.
4. Unresolved dependent placeholders are identified by **typed state**, not by best-effort name inspection.

---

## Recommended data-model direction

I do **not** recommend only replacing `is_dependent + dependent_name` with `std::optional<StringHandle>`.

That would remove one invalid state, but it would still be too weak for the real architectural need. The plan should instead move toward something closer to:

```cpp
struct DependentValueIdentity {
	StringHandle name;
};

struct TemplateValueArg {
	TypeCategory category;
	std::variant<int64_t, DependentValueIdentity> payload;
};
```

The exact final type can differ, but the important design rule is:

- **dependency identity should be carried as a first-class payload**
- **key building should consume that payload directly**
- **stringification/hash/equality should all project from the same carrier**

That gives the cleanup we wanted from `std::optional`, but in a form that can actually become authoritative.

---

## Design questions raised during branch work

### Q: Is there any point in keeping both `TypeInfo::TemplateArgInfo` and `TemplateTypeArg`?

**Short answer:** yes **for now**, but not in their current blurry form.

`TemplateTypeArg` is still the richer working representation:

- deduction/matching state
- dependent-vs-concrete identity
- pack/template-template/member-pointer metadata
- parser/substitution-time manipulation

`TypeInfo::TemplateArgInfo` is the lighter persistent representation owned by instantiated `TypeInfo` entries and their instantiation contexts:

- compact storage on `TypeInfo`
- fewer dependencies from core type records into template-matching logic
- easier persistence of type-owned template environments for later constexpr/codegen/sema lookups

So they are **not fully equivalent today**, even if they overlap heavily.

What *does not* make sense long-term is letting both evolve as peer representations with duplicated semantics. The better direction is:

1. keep a **canonical template-argument identity carrier**
2. let `TemplateTypeArg` stay a rich working/adaptation layer if needed
3. make `TemplateArgInfo` either:
   - a compact serialized/persistent projection of that canonical carrier, or
   - disappear entirely if the canonical carrier is cheap enough to store directly

In other words: **keeping two layers can make sense; keeping two independently authoritative models does not**.

### Q: Does it make sense to keep all `add_type_alias_copy()` overloads?

**Short answer:** not really in their current form.

The overload set is convenient, but it is also bug-prone because alias canonicalization behavior can diverge depending on which overload the caller happened to use. That already showed up in this branch work: the primitive-target fix had to be applied in the `alias_type_spec`-carrying path specifically.

There is still a legitimate distinction between:

- "register an alias from a known canonical target type"
- "register an alias while preserving the source alias type-specifier surface"

But that should probably be expressed through **one authoritative implementation** with explicit inputs, not multiple overload bodies that can drift. A better shape would be:

- one core helper or builder taking:
  - alias name
  - canonical source type/index
  - fallback size
  - optional alias type specifier
- plus, at most, tiny forwarding wrappers if they add no behavior

So the architectural answer is: **keep the semantic distinction, but collapse the overload behavior behind one implementation**.

---

## Concrete implementation plan

## Phase 0: freeze the regression surface first

**Goal**

Keep the recent metaprogramming fixes stable while refactoring the underlying representation.

**Primary files**

- `tests\test_integral_constant_comprehensive_ret100.cpp`
- `tests\test_integral_constant_pattern_ret42.cpp`
- `tests\test_ratio_less_alias_ret0.cpp`
- `tests\test_sfinae_enable_if_ret0.cpp`
- `tests\test_sfinae_same_name_overload_ret0.cpp`

**Concrete work**

1. Treat the five restored tests as a must-pass regression set for every slice below.
2. Add one or two narrow regression tests if a phase changes representation without exercising an existing failure shape:
   - alias chain with dependent non-type argument
   - late-instantiated alias-template use reached through constexpr or qualified lookup
3. Keep the refactor incremental: do not combine Phase 1 and Phase 2 into one large patch.

**Done when**

- every phase can be validated against the current five-test regression set,
- any new failure can be localized to one architectural slice rather than a multi-file rewrite.

---

## Phase 1: canonicalize non-type template-argument identity

**Status:** IN PROGRESS (as of 2026-04-12)

**Completed work:**
1. ✅ Introduced `NonTypeValueIdentity` carrier struct in `src/TemplateTypes.h`
   - Added `value_type` field to capture TypeCategory for value args
   - Added factory methods: `makeConcrete()`, `makeDependent()`
   - Added `toString()` for debugging/name generation  
   - Added `operator==` with Bool/Int interchangeability
   - Added `hash()` consistent with equality
2. ✅ Made `ValueArgKey` an alias for `NonTypeValueIdentity` for backward compatibility
3. ✅ Added `valueIdentity()` accessor to `TemplateTypeArg` 
4. ✅ Updated `makeInstantiationKey()` to use `valueIdentity()` accessor
5. ✅ Updated `TemplateTypeArg::toString()` to delegate to `NonTypeValueIdentity::toString()`
6. ✅ All 2052 tests pass, 132 expected-fail

**Remaining work:**
- Consider removing redundant `is_value + value + is_dependent + dependent_name` fields from `TemplateTypeArg` 
  (would be a larger refactor touching many files)
- Update `toHashString()` and `TemplateTypeArgHash` to share hash logic with `NonTypeValueIdentity`
  (requires careful migration to avoid breaking existing instantiation lookups)

**Goal**

Remove the split-state representation where `TemplateTypeArg` owns one dependency model and `TemplateInstantiationKey` owns another.

**Primary files**

- `src\TemplateRegistry_Types.h`
- `src\TemplateTypes.h`
- `src\TemplateRegistry_Registry.h`
- parser/template helpers that create `TemplateTypeArg` values

**Concrete work**

1. Introduce a first-class carrier for non-type value identity in `TemplateRegistry_Types.h`.
2. Replace the current `is_value + value + is_dependent + dependent_name` split for value arguments with that carrier.
3. Make `TemplateInstantiationKey` consume the same carrier directly instead of re-deriving `ValueArgKey` from loosely related fields.
4. Move hash/equality/string/mangled-name generation behind shared helpers so all projections come from the same identity model.
5. Audit all `TemplateTypeArg` constructors/factories and all "evaluate expression -> make value arg" sites to ensure they stamp the new identity consistently.
6. Keep bool/int equivalence behavior only where it is intentional for partial-specialization/value matching; document that rule right next to equality/hash code.

**Why this phase is first**

The recent regression proved that name/key generation is the last point where distinct instantiations can still collapse even after parser capture is correct.

**Done when**

- there is one canonical representation of a non-type template argument's dependency identity,
- `TemplateInstantiationKey` no longer needs to reconstruct dependency semantics from ad-hoc fields,
- hash/equality/name generation cannot disagree on whether an argument is concrete or dependent.

**Read/query first**

- `src\TemplateRegistry_Types.h:170-183`
- `src\TemplateRegistry_Types.h:348-365`
- `src\TemplateRegistry_Types.h:483-565`
- `src\TemplateRegistry_Types.h:699-721`
- `src\TemplateTypes.h:185-265`
- `src\TemplateTypes.h:345-360`

---

## Phase 2: centralize alias-template materialization

**Goal**

Make alias-template use resolution one parser-owned service instead of a behavior repeated in top-level using, struct-local using, type-specifier parsing, and substitution helpers.

**Primary files**

- `src\Parser.h`
- `src\Parser_Decl_TopLevel.cpp`
- `src\Parser_Decl_TypedefUsing.cpp`
- `src\Parser_TypeSpecifiers.cpp`
- `src\Parser_Templates_Inst_Substitution.cpp`
- `src\ExpressionSubstitutor.cpp`

**Concrete work**

1. Add one helper with a narrow contract, e.g. "given alias template name + raw/concrete args, return the resolved target type/materialized instantiation result".
2. Move these responsibilities into that helper:
   - alias-parameter substitution
   - concrete non-type argument evaluation
   - alias-chain recursion
   - class-template instantiation
   - late materialized struct registration
   - use-site `TypeSpecifierNode` rewrite
3. Change top-level `using Alias = AliasTemplate<...>;` handling to only gather syntax and call the helper.
4. Change struct-local `using` / typedef alias registration to do the same.
5. Change general type-specifier alias-template handling to route through the same helper instead of its own deferred-substitution loop.
6. Audit `ExpressionSubstitutor.cpp` and any other template-substitution sites that currently instantiate aliases/classes directly so they also reuse the shared helper or an adjacent lower-level primitive.

**Important design constraint**

This helper should return more than just a name string. It should carry enough structured result to avoid each caller redoing type lookup and `TypeSpecifierNode` rebuilding differently.

**Done when**

- parser entry points no longer each contain their own alias-template argument-substitution loop,
- alias-chain behavior is consistent regardless of whether the use site is a type alias, a type specifier, or template substitution,
- fixing alias-template behavior in one place updates all surfaces.

**Read/query first**

- `src\Parser_Decl_TopLevel.cpp:827-1071`
- `src\Parser_Decl_TypedefUsing.cpp:410-434`
- `src\Parser_TypeSpecifiers.cpp:995-1235`
- `src\Parser_Templates_Inst_Substitution.cpp:19-180`

---

## Phase 3: make late materialization + pending-sema normalization explicit

**Goal**

Turn the current "register late materialized node, then maybe normalize pending roots if available" pattern into one explicit contract.

**Primary files**

- `src\Parser.h`
- `src\Parser_Core.cpp`
- `src\IrGenerator_Helpers.cpp`
- `src\Parser_Templates_Lazy.cpp`
- `src\Parser_Expr_QualLookup.cpp`
- `src\ConstExprEvaluator_Members.cpp`
- any parser/template instantiation sites that call `registerLateMaterializedTopLevelNode(...)`

**Concrete work**

1. Define the intended lifecycle in code comments and helper naming:
   - materialize AST root
   - register it
   - enqueue it for sema
   - normalize it when a sema owner is active
2. Add one helper that performs the registration/enqueue part together so call sites cannot forget half of the contract.
3. Decide whether normalization belongs:
   - immediately in parser/constexpr call sites when active sema exists, or
   - behind a dedicated queue-drain helper used by parser/constexpr/codegen bridges
4. Replace direct scattered `normalizePendingSemanticRootsIfAvailable()` calls with that one policy.
5. Audit current late-instantiation sites from class-template instantiation, lazy members, qualified lookup, constexpr member lookup, and substitution paths.
6. Ensure the ownership rule is the same regardless of whether the instantiation was triggered by parser lookup, constexpr evaluation, or IR/codegen-side lazy generation.

**Why this matters**

The current architecture already has the pieces for incremental sema normalization. What it lacks is one obvious, enforced path that every new late-instantiation site must use.

**Done when**

- there is one obvious helper or queue contract for late-materialized roots,
- new instantiation sites do not need to remember "register here, normalize there",
- constexpr-triggered and parser-triggered materialization follow the same normalization rule.

**Read/query first**

- `src\Parser.h:976-993`
- `src\Parser_Core.cpp:419-423`
- `src\IrGenerator_Helpers.cpp:5-11`
- `src\ConstExprEvaluator_Members.cpp:1319-1326`
- the `registerLateMaterializedTopLevelNode(...)` call sites across parser/template files

---

## Phase 4: replace unresolved-placeholder heuristics with explicit state

**Goal**

Stop detecting important dependent placeholder cases by combining incomplete-instantiation state with string-level name inspection.

**Primary files**

- `src\Parser_Templates_Inst_Deduction.cpp`
- `src\TypeInfo*` / template-registry carrier types that currently model placeholder state
- any alias/late-instantiation code that creates dependent placeholder types

**Concrete work**

1. Identify the exact placeholder states that currently surface as "incomplete instantiation with a `::` in the name".
2. Add an explicit kind/flag for unresolved dependent member-alias placeholders or equivalent unresolved-dependent materialization states.
3. Use that explicit state in SFINAE viability checks.
4. Audit whether the same explicit state should also guide alias-template resolution, late instantiation lookup, and constexpr member lookup.
5. Remove or narrow the current string heuristic once the explicit state is available everywhere it is needed.

**Done when**

- SFINAE viability checks do not need to infer unresolved dependent state from names,
- placeholder categories are visible in the type/instantiation model itself,
- future dependent-alias bugs are easier to reason about from debugger/log output.

**Read/query first**

- `src\Parser_Templates_Inst_Deduction.cpp:2175-2190`

---

## Phase 5: only then consider the larger parser/sema ownership move

**Goal**

Use the earlier cleanup to make the broader parser/template/sema boundary work practical rather than theoretical.

**Primary references**

- `docs\2026-04-06-template-constexpr-sema-audit-plan.md`
- `docs\2026-03-21_PARSER_TEMPLATE_SEMA_BOUNDARY_PLAN.md`

**Concrete work**

1. Revisit whether alias-template instantiation or more late-instantiation logic should move behind a sema-owned incremental work queue.
2. Decide whether template materialization should stay parser-owned with stronger invariants, or whether some subset should become sema-triggered.
3. Keep this as a follow-on after Phases 1-4, not as a prerequisite for them.

**Why this is last**

The recent bug did not require a full architecture rewrite. It exposed specific duplication and split identity first. Removing those is the cheaper and safer way to earn clearer sema ownership later.

---

## Validation matrix

Each phase should at minimum run:

- `.\build_flashcpp.bat Sharded`
- `powershell -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1 test_integral_constant_comprehensive_ret100.cpp test_integral_constant_pattern_ret42.cpp test_ratio_less_alias_ret0.cpp test_sfinae_enable_if_ret0.cpp test_sfinae_same_name_overload_ret0.cpp`

Recommended targeted additions during the refactor:

1. alias chain with dependent bool non-type argument
2. alias-template use through top-level `using`
3. alias-template use through struct-local `using`
4. dependent `enable_if<..., T>::type` return-type viability
5. late-instantiated member/template path that requires pending-sema normalization after materialization

---

## Risks and guardrails

### Risk 1: changing value-arg identity can perturb specialization matching

Guardrail:

- keep bool/int normalization only where the current semantics depend on it,
- do not silently change equality/hash rules without matching tests.

### Risk 2: centralizing alias materialization can accidentally drop use-site qualifiers

Guardrail:

- treat alias target resolution and use-site pointer/reference/cv/array modifiers as separate steps,
- preserve the current merge behavior when rebuilding `TypeSpecifierNode`.

### Risk 3: eager normalization hooks can create re-entrancy surprises

Guardrail:

- make the registration/queue contract explicit before widening normalization calls,
- prefer a queue-drain helper over hidden recursive normalization in many unrelated sites.

### Risk 4: placeholder-state cleanup can overfit one SFINAE bug

Guardrail:

- model unresolved-dependent state broadly enough that it also explains alias-member placeholders and other dependent `::type` shapes,
- do not bake test-specific string patterns into the final representation.

---

## Reviewer-identified issues and refactoring opportunities

The following items were raised during code review (Gemini) and should be addressed as part of or alongside the phases above.

### Dead `else if` in `ExpressionSubstitutor.cpp` (two sites)

At `src/ExpressionSubstitutor.cpp:744` and `src/ExpressionSubstitutor.cpp:1170`, `instantiated_name` is unconditionally assigned `template_name_to_instantiate` (which is non-empty), making the subsequent `else if (instantiated_name.empty())` always false. `get_instantiated_class_name` is therefore dead code. The fix is to try the registry lookup first, then fall back to `get_instantiated_class_name`, and only use the raw template name as a last resort. **Relates to Phase 2.**

### Alias resolution duplication between top-level and struct-local `using`

`src/Parser_Decl_TopLevel.cpp:1019-1054` and `src/Parser_Decl_TypedefUsing.cpp:410-434` both contain the same pattern: check `isTemplateInstantiation`, materialize args via `toTemplateTypeArg`, call `instantiate_and_register_base_template`, and update the `TypeSpecifierNode`. Extract a shared helper (e.g. `resolveAndRegisterAliasTarget`). **Relates to Phase 2.**

### Expression substitution duplication across instantiation sites

`src/Parser_Templates_Inst_ClassTemplate.cpp:441-454` duplicates the `ExpressionSubstitutor` + `ConstExpr::Evaluator::evaluate` dispatch pattern that appears at 9+ other sites in this PR. Extract a shared helper (e.g. `TemplateTypeArg fromEvalResult(const ConstExpr::EvalResult&)` plus a `substituteAndEvaluateNonTypeDefault` wrapper). **Relates to Phase 2.**

### Extract `materialize_placeholder_args` from deduction

The `materialize_placeholder_args` lambda at `src/Parser_Templates_Inst_Deduction.cpp:2059-2178` is ~120 lines. It should be a dedicated `Parser` member function for readability and testability. **Relates to Phase 2 / Phase 3.**

### Extract `normalizeDependentNonTypeArgs` from `parse_type_specifier`

The `normalizeDependentNonTypeArgs` lambda at `src/Parser_TypeSpecifiers.cpp:961-988` is complex and likely needed in other parts of the template parsing pipeline. It should be a dedicated helper method on `Parser`. **Relates to Phase 1.**

### Hash strategy drift between `TemplateTypeArg::hash()` and `ValueArgKey::hash()`

~~`TemplateTypeArg::hash()` (`src/TemplateRegistry_Types.h:267-272`) uses `std::hash<StringHandle>` for `dependent_name`, while `ValueArgKey::hash()` (`src/TemplateTypes.h:196-203`) uses `std::hash<uint32_t>` on the raw `.handle` field. The two types are never compared in the same hash map so this is not a correctness bug today, but it is exactly the kind of drift that Phase 1 canonicalization should eliminate.~~ **ADDRESSED in Phase 1 (2026-04-12):** `ValueArgKey` is now an alias for `NonTypeValueIdentity`, which uses `std::hash<StringHandle>{}(dependent_name)` consistently with `TemplateTypeArg::hash()`.

---

## Phase 6: unify template-parameter-to-function-parameter deduction mapping

**Date added:** 2026-04-11
**Context:** PR #1207 exposed that `try_instantiate_template_explicit` uses a positional counter (`deduced_call_arg_index`) to map template parameters to call arguments. This is architecturally incorrect — it assumes template parameter `i` corresponds to function parameter `i`, which breaks for SFINAE guards, non-deducible params, and reordered mappings.

### Current state

There are **two separate deduction paths** with different mapping strategies:

1. **`try_instantiate_template_explicit`** (`src/Parser_Templates_Inst_Deduction.cpp:446-1159`)
   — the "explicit template args + call args" path. Uses a blind positional counter
   `deduced_call_arg_index` that walks `current_explicit_call_arg_types_` in order.
   No mapping between template parameter names and function parameter types.

2. **`try_instantiate_single_template`** (`src/Parser_Templates_Inst_Deduction.cpp:1282-1670`)
   — the "deduce everything from call args" path. Has a **pre-deduction pass**
   (lines 1361-1522) that builds a `param_name_to_arg` map by inspecting function
   parameter types against template parameter names. This is architecturally correct.

### The problem with `deduced_call_arg_index`

For `template<typename T, typename = decltype(swap(...))> true_type test(int)` called as
`test<pair<const int,int>>(0)`:

- Template param 0 (`T`): consumed from explicit args ✓
- Template param 1 (SFINAE guard): `deduced_call_arg_index=0` still points to the `int`
  call arg → **incorrectly consumes it** instead of using the default

The current workaround (PR #1207) gates call-arg deduction on `!param.has_default()`.
This works for SFINAE guards (which always have defaults) but is not C++20-correct for
`template<typename T, typename U = int> void foo(T, U)` called as `foo<double>(1.0, "hello")`
— `U` should be deduced as `const char*` from the 2nd call arg, but the workaround uses
the default `int` because `U` has a default.

### Recommended architectural fix

Add a **pre-deduction pass** to `try_instantiate_template_explicit`, analogous to the one
in `try_instantiate_single_template`. Specifically:

1. **Build a `param_name_to_arg` map** before the main template-arg-building loop.
   Walk the function parameter list (`func_decl.parameter_nodes()`), check each
   parameter's type against the template parameter name set, and match against
   `current_explicit_call_arg_types_` by position in the *function* parameter list
   (not the template parameter list).

2. **In the main loop**, when a template parameter isn't covered by explicit args:
   - Check `param_name_to_arg` first (name-based deduction from function params)
   - Then fall back to `appendDefaultTemplateArg` (defaults)
   - Then fail with overload mismatch

3. **Remove `deduced_call_arg_index`** entirely — it is the root cause of the
   positional mapping bug.

### Why this is correct

The pre-deduction approach works because it maps template parameters to function
parameters **by name** rather than by position:

- For `template<typename T, typename U> void foo(T a, U b)` called as `foo<double>(1.0, "hello")`:
  function param 0 has type `T` → maps to template param `T` (already explicit)
  function param 1 has type `U` → maps to call arg 1 (`"hello"`) → deduces `const char*`

- For `template<typename T, typename = decltype(...)> true_type test(int)`:
  function param 0 has type `int` (concrete) → no template param mapping
  template param 1 (SFINAE guard) has no function param → falls through to default

### Shared helper opportunity

The pre-deduction logic in `try_instantiate_single_template` (lines 1361-1522) and the
proposed new logic for `try_instantiate_template_explicit` are structurally identical.
They should be extracted into a shared helper, e.g.:

```cpp
// Build a map from template parameter names to deduced TemplateTypeArgs
// by matching function parameter types against call argument types.
std::unordered_map<StringHandle, TemplateTypeArg>
Parser::buildDeductionMapFromCallArgs(
    const std::vector<ASTNode>& template_params,
    const FunctionDeclarationNode& func_decl,
    const std::vector<TypeSpecifierNode>& call_arg_types);
```

This would eliminate ~160 lines of duplication and ensure both paths use the same
deduction logic.

### Files to change

- `src/Parser_Templates_Inst_Deduction.cpp` — add pre-deduction pass to
  `try_instantiate_template_explicit`, extract shared helper from
  `try_instantiate_single_template`
- `src/Parser.h` — declare the shared helper

### Validation

- `test_namespaced_pair_swap_sfinae_ret0.cpp` — SFINAE guard with `decltype` default
- `test_pack_decltype_simple_ret42.cpp` — pack expansion in `decltype` base class
- New test needed: `template<typename T, typename U = int> void foo(T, U)` called as
  `foo<double>(1.0, "hello")` to verify `U` is deduced as `const char*` (not `int`)
- Full test suite regression check

---

## Out of scope for this document

- a full rewrite that moves all template instantiation out of the parser immediately
- a full constexpr architecture rewrite
- unrelated codegen/sema ownership cleanup outside the template-materialization surface

---

## Related docs

- `docs\2026-04-06-template-constexpr-sema-audit-plan.md`
- `docs\2026-04-06-constructor-overload-resolution-refactor.md`
- `docs\2026-04-04-codegen-name-lookup-investigation.md`
- `docs\2026-03-21_PARSER_TEMPLATE_SEMA_BOUNDARY_PLAN.md`

---

## Bottom line

The branch fix solved the immediate regression by teaching instantiation keys to distinguish dependent and concrete non-type arguments. The next worthwhile architectural step is to make that distinction authoritative everywhere, then collapse alias-template materialization and late-materialization normalization onto explicit shared helpers.

That is the highest-leverage follow-up because it attacks the actual fault line that produced the recent failures: duplicated template identity logic plus duplicated materialization paths.
