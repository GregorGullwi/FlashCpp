# Expected Return Values for FlashCpp Tests

This document tracks the expected return values for test files and documents any regressions found during validation.

## Overview

Many test files in the `tests/` directory follow the naming convention `test_name_retNN.cpp` where `NN` is the expected return value from the `main()` function. The `validate_return_values.sh` script validates that tests return their expected values.

## Validation Summary

**Last Run:** 2026-01-03

**Total files tested:** 817
**Valid returns (matching expected):** 784
**Regressions (mismatches):** 20
**Runtime crashes:** 13
**Compile failures:** 0
**Link failures:** 0

## Regressions Found

The following test files have a mismatch between their expected return value (from filename) and the actual return value:

| Test File | Expected | Actual | Status |
|-----------|----------|--------|--------|
| integer_arithmetic_ret18.cpp | 18 | 33 | REGRESSION |
| spaceship_basic_ret255.cpp | 255 | 253 | REGRESSION |
| test_all_mix_ret123.cpp | 123 | 125 | REGRESSION |
| test_auto_trailing_return_ret42.cpp | 42 | 192 | REGRESSION |
| test_comma_init_ret197.cpp | 197 | 42 | REGRESSION |
| test_container_out_of_line_ret60.cpp | 60 | 72 | REGRESSION |
| test_covariant_return_ret180.cpp | 180 | 111 | REGRESSION |
| test_global_namespace_scope_ret1.cpp | 1 | 203 | REGRESSION |
| test_inherited_type_alias_ret42.cpp | 42 | 0 | REGRESSION |
| test_lambda_init_capture_demo_ret57.cpp | 57 | 70 | REGRESSION |
| test_qualified_base_class_ret42.cpp | 42 | 0 | REGRESSION |
| test_simple_range_ret6.cpp | 6 | 74 | REGRESSION |
| test_sizeof_template_param_default_ret4.cpp | 4 | 1 | REGRESSION |
| test_static_constexpr_pack_value_ret42.cpp | 42 | 0 | REGRESSION |
| test_std_header_features_ret0.cpp | 0 | 8 | REGRESSION |
| test_structured_binding_lvalue_ref_ret52.cpp | 52 | 20 | REGRESSION |
| test_template_disambiguation_pack_ret40.cpp | 40 | 0 | REGRESSION |
| test_template_param_typename_default_ret42.cpp | 42 | 16 | REGRESSION |
| test_type_alias_fix_simple_ret42.cpp | 42 | 0 | REGRESSION |
| test_void_t_positive_ret0.cpp | 0 | 42 | REGRESSION |

These values come from the 2026-01-03 run of `tests/validate_return_values.sh`. When a regression is triaged, add a short note or link next to the entry to preserve context.

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
