# Test Runner Failure Report

## Clang Run Summary

- **Runner:** `tests/run_all_tests.sh --clang`
- **Date:** 2026-06-02
- **Compiler:** `clang++`
- **Files tested:** 2581
- **Skipped:** 31 Windows-only SEH tests

## Failing Tests

### Compile failures

- `concept_comprehensive_ret15.cpp`
- `countof_test.cpp`
- `partial_spec_pattern_collision_ret0.cpp`
- `problem_statement_example.cpp`
- `spaceship_default.cpp`
- `test_complex_keywords.cpp`
- `test_constexpr_lambda_immediate_ret0.cpp`
- `test_constexpr_offsetof_nested_ret0.cpp`
- `test_constexpr_offsetof_ret0.cpp`
- `test_copy_assign_default_arg_ret42.cpp`
- `test_ctor_string_literal_lvalue_ret0.cpp`
- `test_declspec_class_ret0.cpp`
- `test_declspec_dllimport_var_ret42.cpp`
- `test_dependent_decltype_arrow_member_pointer_ret0.cpp`
- `test_dependent_decltype_member_pointer_local_ret0.cpp`
- `test_extern_template_does_not_instantiate_ret0.cpp`
- `test_friend_default_spaceship_ret0.cpp`
- `test_identifier_binding_constexpr_function_call_member_access_prefers_static_member_function_ret42.cpp`
- `test_infer_expr_type_expansion_ret0.cpp`
- `test_local_declspec_attribute_prefix_ret42.cpp`
- `test_member_struct_partial_spec_ret1.cpp`
- `test_member_template_nested_static_member_two_phase_definition_lookup_ret0.cpp`
- `test_no_unique_address_empty_member_same_type_overlap_ret0.cpp`
- `test_operator_addressof_overload_baseline_ret99.cpp`
- `test_operator_member_tiebreak_ret0.cpp`
- `test_outofline_nested_pack_ret0.cpp`
- `test_outofline_nested_union_ret0.cpp`
- `test_pack_expansion_comprehensive.cpp`
- `test_rtti_basic_ret1.cpp`
- `test_separator_chars.cpp`
- `test_sizeof_offsetof.cpp`
- `test_sizeof_template_param_default_ret4.cpp`
- `test_spaceship_3member_ret15.cpp`
- `test_spaceship_all_ops_ret255.cpp`
- `test_spaceship_inline_expr_ret17.cpp`
- `test_spaceship_longlong_ret15.cpp`
- `test_spaceship_mixed_types_ret3.cpp`
- `test_spaceship_multi_member_ret8.cpp`
- `test_spaceship_negative_ret31.cpp`
- `test_spaceship_nested_delegate_ret7.cpp`
- `test_spaceship_reversed_ret31.cpp`
- `test_spaceship_signed_cmp_ret127.cpp`
- `test_spaceship_synthesized_ret8.cpp`
- `test_spaceship_template_ret127.cpp`
- `test_spaceship_user_synth_ret255.cpp`
- `test_static_constexpr_member_partial_spec_ret4.cpp`
- `test_std_forward.cpp`
- `test_std_forward_observable.cpp`
- `test_stdlib_features_ret0.cpp`
- `test_structured_binding_member_get_preferred_over_free_get_ret42.cpp`
- `test_template_arg_context_ambiguous_type_vs_value_ret0.cpp`
- `test_template_inclass_static_member_two_phase_lookup_ret0.cpp`
- `test_template_nested_ool_member_template_outer_param_binding_ret0.cpp`
- `test_template_qualified_phase1_fallback_ret0.cpp`
- `test_template_static_member_initializer_replay_metadata_invariant_ret0.cpp`
- `test_template_static_member_lazy_forward_reference_ret42.cpp`
- `test_toplevel_const_ptr_arg_ret0.cpp`
- `test_type_alias_callconv_function_pointer_noexcept_ret0.cpp`
- `test_type_alias_in_sfinae_ret42.cpp`
- `test_type_traits_intrinsics_ret147.cpp`
- `test_typeid_builtin_matches_type_ret0.cpp`
- `test_typeid_expr_matches_type_ret0.cpp`
- `test_typeid_non_polymorphic_expr_matches_type_ret0.cpp`
- `test_typeid_runtime_ref_call_ret0.cpp`
- `test_typeid_runtime_ref_member_evaluated_once_ret0.cpp`
- `test_typeinfo_compare_ret2.cpp`
- `test_va_float_args_ret0.cpp`
- `test_va_implementation_ret60.cpp`
- `test_va_large_struct_ret0.cpp`
- `test_va_mixed_types_ret0.cpp`
- `test_va_struct_args_ret0.cpp`
- `test_varargs.cpp`
- `test_variadic_overload.cpp`

### Return mismatches

- `test_explicit_template_overload_cache_split_ret7.cpp` — expected `7`, got `42`
- `test_member_init_mixed_ret40.cpp` — expected `40`, got `176` and `183`
- `test_nested_member_template_static_bool_nttp_alias_ret0.cpp` — expected `0`, got `1`
- `test_new_intrinsics_ret0.cpp` — expected `0`, got `1`

## Notes

- Compile failures: 72 unique tests
- Link failures: 0
- Runtime crashes: 0
- Return mismatches: 4 unique tests
- `_fail` files that unexpectedly passed: 0

## Result

`RESULT: FAILED`
