# Test Runner Failure Report

## Clang Run Summary

- **Runner:** `tests/run_all_tests.sh --clang`
- **Date:** 2026-06-02
- **Compiler:** `clang++`
- **Files tested:** 2581
- **Skipped:** 31 Windows-only SEH tests

## Failing Tests

### Compile failures

- `problem_statement_example.cpp`
- `spaceship_default.cpp`
- `test_complex_keywords.cpp`
- `test_constexpr_lambda_immediate_ret0.cpp`
- `test_constexpr_offsetof_nested_ret0.cpp`
- `test_constexpr_offsetof_ret0.cpp`
- `test_declspec_class_ret0.cpp`
- `test_declspec_dllimport_var_ret42.cpp`
- `test_friend_default_spaceship_ret0.cpp`
- `test_identifier_binding_constexpr_function_call_member_access_prefers_static_member_function_ret42.cpp`
- `test_infer_expr_type_expansion_ret0.cpp`
- `test_local_declspec_attribute_prefix_ret42.cpp`
- `test_no_unique_address_empty_member_same_type_overlap_ret0.cpp`
- `test_outofline_nested_pack_ret0.cpp`
- `test_outofline_nested_union_ret0.cpp`
- `test_rtti_basic_ret1.cpp`
- `test_sizeof_offsetof.cpp`
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
- `test_std_forward.cpp`
- `test_std_forward_observable.cpp`
- `test_stdlib_features_ret0.cpp`
- `test_structured_binding_member_get_preferred_over_free_get_ret42.cpp`
- `test_toplevel_const_ptr_arg_ret0.cpp`
- `test_type_alias_callconv_function_pointer_noexcept_ret0.cpp`
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

### Return mismatches

- `test_explicit_template_overload_cache_split_ret7.cpp` — expected `7`, got `42`
- `test_member_init_mixed_ret40.cpp` — expected `40`, got `176` and `183`
- `test_nested_member_template_static_bool_nttp_alias_ret0.cpp` — expected `0`, got `1`
- `test_new_intrinsics_ret0.cpp` — expected `0`, got `1`

## Notes

- Compile failures: 50 unique tests
- Link failures: 0
- Runtime crashes: 0
- Return mismatches: 4 unique tests
- `_fail` files that unexpectedly passed: 0

## C++20 Reject Triage

This is a first-pass triage using:

```text
clang++ --target=x86_64-unknown-linux-gnu -std=c++20 -pedantic-errors -fsyntax-only
```

The goal here is only to separate genuine standard C++20 reject cases from:

- missing required standard headers
- vendor extensions / target-specific ABI assumptions

No test files were changed in this pass.

### Ignored For Now: Missing Required Headers

These are not interesting language-invalid cases yet; they need the right standard headers first.

- `problem_statement_example.cpp`: defaulted spaceship requires `<compare>`.
- `spaceship_default.cpp`: defaulted spaceship requires `<compare>`.
- `test_friend_default_spaceship_ret0.cpp`: defaulted spaceship requires `<compare>`.
- `test_rtti_basic_ret1.cpp`: uses `typeid` without including `<typeinfo>`.
- `test_spaceship_3member_ret15.cpp`: defaulted spaceship requires `<compare>`.
- `test_spaceship_all_ops_ret255.cpp`: defaulted spaceship requires `<compare>`.
- `test_spaceship_inline_expr_ret17.cpp`: defaulted spaceship requires `<compare>`.
- `test_spaceship_longlong_ret15.cpp`: defaulted spaceship requires `<compare>`.
- `test_spaceship_mixed_types_ret3.cpp`: defaulted spaceship requires `<compare>`.
- `test_spaceship_multi_member_ret8.cpp`: defaulted spaceship requires `<compare>`.
- `test_spaceship_negative_ret31.cpp`: defaulted spaceship requires `<compare>`.
- `test_spaceship_nested_delegate_ret7.cpp`: defaulted spaceship requires `<compare>`.
- `test_spaceship_reversed_ret31.cpp`: defaulted spaceship requires `<compare>`.
- `test_spaceship_signed_cmp_ret127.cpp`: defaulted spaceship requires `<compare>`.
- `test_spaceship_synthesized_ret8.cpp`: defaulted spaceship requires `<compare>`.
- `test_spaceship_template_ret127.cpp`: defaulted spaceship requires `<compare>`.
- `test_std_forward.cpp`: uses `std::forward` without including the needed standard header.
- `test_std_forward_observable.cpp`: uses `std::forward` without including the needed standard header.
- `test_typeid_builtin_matches_type_ret0.cpp`: uses `typeid` without including `<typeinfo>`.
- `test_typeid_expr_matches_type_ret0.cpp`: uses `typeid` without including `<typeinfo>`.
- `test_typeid_non_polymorphic_expr_matches_type_ret0.cpp`: uses `typeid` without including `<typeinfo>`.
- `test_typeid_runtime_ref_call_ret0.cpp`: uses `typeid` without including `<typeinfo>`.
- `test_typeid_runtime_ref_member_evaluated_once_ret0.cpp`: uses `typeid` without including `<typeinfo>`.
- `test_typeinfo_compare_ret2.cpp`: uses `typeid` / `type_info` without including `<typeinfo>`.

### Ignored For Now: Extensions Or ABI-Specific Assumptions

These are outside portable standard C++20 and should be tracked separately from real language-invalid tests.

- `test_complex_keywords.cpp`: uses C99 `_Complex`, `__complex__`, and `_Imaginary`.
- `test_declspec_class_ret0.cpp`: uses MSVC `__declspec(...)` syntax.
- `test_declspec_dllimport_var_ret42.cpp`: uses MSVC `__declspec(dllimport)`.
- `test_local_declspec_attribute_prefix_ret42.cpp`: uses MSVC `__declspec(align(...))`.
- `test_stdlib_features_ret0.cpp`: relies on compiler intrinsic `__is_complete_or_unbounded`.
- `test_toplevel_const_ptr_arg_ret0.cpp`: uses `__cdecl` and `unsigned __int64`.
- `test_type_alias_callconv_function_pointer_noexcept_ret0.cpp`: uses `__stdcall`.
- `test_va_float_args_ret0.cpp`: assumes a particular `va_list` / builtin varargs ABI model.
- `test_va_implementation_ret60.cpp`: assumes a particular `va_list` / builtin varargs ABI model.
- `test_va_large_struct_ret0.cpp`: assumes a particular `va_list` / builtin varargs ABI model.
- `test_va_mixed_types_ret0.cpp`: assumes a particular `va_list` / builtin varargs ABI model.
- `test_va_struct_args_ret0.cpp`: assumes a particular `va_list` / builtin varargs ABI model.
- `test_varargs.cpp`: includes helper code that hardcodes a non-portable `va_list` representation.

### Genuine C++20 Reject Cases

These still reject after setting aside header omissions and extension-only cases.

- `test_constexpr_offsetof_nested_ret0.cpp`: uses `offsetof(PackedOuter, inner.value)`; nested member designators are rejected by this standard-library `offsetof` form.
- `test_constexpr_offsetof_ret0.cpp`: uses `offsetof` without first making the macro available, so the token sequence is parsed as ordinary code and becomes invalid.
- `test_identifier_binding_constexpr_function_call_member_access_prefers_static_member_function_ret42.cpp`: initializes a `constexpr` data member from a function call that is not accepted as a constant expression in this form.
- `test_infer_expr_type_expansion_ret0.cpp`: uses `offsetof` without first making the macro available, so the expression is not parsed as an `offsetof` invocation.
- `test_no_unique_address_empty_member_same_type_overlap_ret0.cpp`: uses `offsetof` without first making the macro available, so the expression is invalid as written.
- `test_outofline_nested_pack_ret0.cpp`: uses `offsetof(Wrapper<int>::Nested, x)` without making `offsetof` available.
- `test_outofline_nested_union_ret0.cpp`: uses `offsetof(Wrapper<int>::Nested, a)` without making `offsetof` available.
- `test_sizeof_offsetof.cpp`: uses `offsetof` without first making the macro available.
- `test_spaceship_user_synth_ret255.cpp`: expects the relational operators to synthesize from a user-defined `operator<=>` returning `int`; that is not how C++20 synthesis works.
- `test_structured_binding_member_get_preferred_over_free_get_ret42.cpp`: provides an invalid `get` customization / specialization shape, so the intended structured-binding lookup does not match.

### Not Reproduced As C++20 Rejects In This Pass

These are still listed in the original failure report, but this syntax-only triage did not reject them as standard-invalid:

- `test_constexpr_lambda_immediate_ret0.cpp`
- `test_type_traits_intrinsics_ret147.cpp`

## Result

`RESULT: FAILED`
