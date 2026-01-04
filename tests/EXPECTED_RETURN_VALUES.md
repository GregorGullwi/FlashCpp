# Expected Return Values for FlashCpp Tests

This document tracks the expected return values for test files and documents any regressions found during validation.

## Overview

Many test files in the `tests/` directory follow the naming convention `test_name_retNN.cpp` where `NN` is the expected return value from the `main()` function. The `validate_return_values.sh` script validates that tests return their expected values.

## Validation Summary

**Last Run:** 2026-01-04 (after void_t SFINAE and pack expansion fixes)

**Total files tested:** 817
**Valid returns (matching expected):** 794 (up from 793)
**Regressions (mismatches):** 10 (down from 11)
**Runtime crashes:** 13
**Compile failures:** 0
**Link failures:** 0

## Fixed Regressions

The following regressions have been FIXED:

| Test File | Expected | Was Returning | Fixed By | Notes |
|-----------|----------|---------------|----------|-------|
| test_all_mix_ret123.cpp | 123 | 125 | Modulo fix | Was returning 125 due to modulo returning 2 instead of 0 |
| test_comma_init_ret197.cpp | 197 | 200 | Assignment fix | Char variables were being sign-extended during assignment |
| test_template_param_typename_default_ret42.cpp | 42 | 16 | Assignment fix (side effect) | Fixed as side effect of assignment operator fix |
| test_auto_trailing_return_ret42.cpp | 42 | 192 | Template parameter substitution | Function parameters were incorrectly substituted with first template argument |
| test_structured_binding_lvalue_ref_ret52.cpp | 52 | 20 | (Already fixed) | Likely fixed by previous commit |
| test_simple_range_ret6.cpp | 6 | 169 | (Already fixed) | Was incorrectly using 64-bit registers for 32-bit int ops |
| test_container_out_of_line_ret60.cpp | 60 | 232 | (Already fixed) | Was incorrectly using 64-bit registers for 32-bit int ops |
| test_static_constexpr_pack_value_ret42.cpp | 42 | 0 | sizeof... pack expansion | Nested binary expressions with static_cast<int>(sizeof...(Ts)) were not handled |
| test_inherited_type_alias_ret42.cpp | 42 | 0 | Self-referential type alias | Type alias `using type = bool_constant;` inside `bool_constant` now correctly points to instantiated type |
| test_type_alias_fix_simple_ret42.cpp | 42 | 0 | (Already fixed) | Static constexpr was properly initialized |

## Regressions Found

The following test files still have a mismatch between their expected return value (from filename) and the actual return value:

| Test File | Expected | Actual | Status | Root Cause |
|-----------|----------|--------|--------|------------|
| test_covariant_return_ret180.cpp | 180 | crash | REGRESSION | Virtual function covariant return issue |
| test_global_namespace_scope_ret1.cpp | 1 | 203 | EXPECTED | Global namespace resolution (sum=1025, 1025 % 256 = 1) |
| test_lambda_init_capture_demo_ret57.cpp | 57 | 73 | TEST ISSUE | File naming issue - clang returns 68, test says expected 68 |
| test_qualified_base_class_ret42.cpp | 42 | 0 | REGRESSION | Deferred template base with member type alias not resolved |
| test_sizeof_template_param_default_ret4.cpp | 4 | 1 | REGRESSION | Template array size not substituted correctly |
| test_std_header_features_ret0.cpp | 0 | 8 | REGRESSION | Type trait/constexpr evaluation |
| test_template_disambiguation_pack_ret40.cpp | 40 | 30 | REGRESSION | Function template specializations with different template args get same mangled name (sum<int> and sum<int,int> both mangle to _ZN2ns3sumEv) |
| test_void_t_positive_ret0.cpp | 0 | 0 | FIXED | void_t SFINAE pattern now correctly detected |

These values come from the 2026-01-04 run. When a regression is triaged, add a short note or link next to the entry to preserve context.

## Root Cause Summary

The remaining regressions fall into these categories:

1. **Template Specialization Selection** (2 tests): Template specializations with explicit arguments are not being selected correctly. The primary template is called instead of the specialization.

2. **SFINAE Issues** (1 test): void_t SFINAE pattern doesn't select the correct specialization.

3. **Template Array Size Substitution** (1 test): Non-type template parameters used as array sizes are not being substituted correctly during template instantiation.

4. **Deferred Template Base Resolution** (1 test): Template base classes with dependent type member aliases (e.g., `detail::select_base<T>::type`) are not being resolved correctly.

5. **Type Trait Evaluation** (1 test): Complex constexpr type traits are not being evaluated correctly.

6. **Other** (1 test): Virtual function covariant returns cause runtime crashes.

## Runtime Crashes

The following test files crash at runtime (signal 11 - Segmentation Fault):

1. test_pack_expansion_simple.cpp
2. test_no_access_control_flag.cpp
3. test_perfect_forwarding_advanced.cpp
4. test_operator_plus_overload_ret15.cpp
5. test_std_forward.cpp
6. test_xvalue_all_casts.cpp
7. test_template_complex_substitution_ret3.cpp
8. test_exceptions_basic.cpp
9. test_varargs.cpp
10. test_return_pointer_ret100.cpp
11. test_operator_addressof_overload_baseline.cpp
12. test_operator_addressof_resolved_ret100.cpp
13. test_exceptions_nested.cpp

## Notes

- On Unix/Linux systems, return values are masked to 0-255 (modulo 256)
- Return values > 255 are automatically truncated (e.g., 300 becomes 44)
- The validation script considers this expected behavior, not a crash
- Some tests may intentionally return values > 255 to test arithmetic features

## How to Use This Document

1. Before making changes to the compiler, run `tests/validate_return_values.sh`
2. Compare results with this document to identify new regressions
3. Add any new regressions to the table above
4. Update the validation summary with the new run date and statistics
5. When fixing a regression, update its status or remove it from the table

## Running the Validation Script

```bash
# From the repository root
./tests/validate_return_values.sh
```

The script will:
- Build the compiler if needed
- Compile and run all test files with `main()`
- Check return values against filenames
- Report any mismatches, crashes, or compilation failures

## Interpreting Results

When running the validation script, results are color-coded in the terminal output:

- **OK**: Test returned the expected value (displayed in green)
- **EXPECTED FAIL**: Test is expected to fail compilation (displayed in yellow)
- **EXPECTED LINK FAIL**: Test is expected to fail linking (displayed in yellow)
- **COMPILE FAIL**: Unexpected compilation failure (displayed in red)
- **LINK FAIL**: Unexpected link failure (displayed in red)
- **CRASH**: Runtime crash/segfault (displayed in red)
- **TIMEOUT**: Test hung or infinite loop (displayed in red)
