# Expected Return Values for FlashCpp Tests

This document tracks the expected return values for test files and documents any return mismatches found during validation.

## Overview

Many test files in the `tests/` directory follow the naming convention `test_name_retNN.cpp` where `NN` is the expected return value from the `main()` function. The `run_all_tests.sh` script validates that tests return their expected values.

## Validation Summary

**Last Run:** 2026-01-28

**Total files tested:** 961
**Valid returns:** 942
**Return mismatches:** 19
**Runtime crashes:** 0
**Ignored files:** 0
**Compile failures:** 0
**Link failures:** 0

## Known Return Mismatches

  template_multi_param_ret63.cpp
  test_array_static_size_ret0.cpp
  test_c_style_casts_ret65.cpp
  test_ctad_struct_lifecycle_ret0.cpp
  test_exceptions_nested_ret0.cpp
  test_feature_macros_ret0.cpp
  test_lambda_copy_this_multiple_lambdas_ret84.cpp
  test_member_alias_in_partial_spec_ret0.cpp
  test_mixed_abi_ret0.cpp
  test_new_intrinsics_ret1.cpp
  test_rvo_cannot_apply_ret0.cpp
  test_rvo_large_struct_ret0.cpp
  test_rvo_mixed_types_ret0.cpp
  test_sfinae_same_name_overload_ret0.cpp
  test_spec_func_ptr_ret0.cpp
  test_spec_init_simple_ret0.cpp
  test_std_move_support_ret0.cpp
  test_xvalue_all_casts_ret0.cpp
  test_xvalue_move_ret0.cpp


## Runtime Crashes

No tests currently crash at runtime.

## Notes

- On Unix/Linux systems, return values are masked to 0-255 (modulo 256)
- Return values > 255 are automatically truncated (e.g., 300 becomes 44)
- The validation script considers this expected behavior, not a crash
- Some tests may intentionally return values > 255 to test arithmetic features

## How to Use This Document

1. Before making changes to the compiler, run `tests/run_all_tests.sh`
2. Compare results with this document to identify new regressions (each failing test row starts with ```[RETURN MISMATCH]```)
3. Add any new regressions to the table above
4. Update the validation summary with the new run date and statistics
5. When fixing a regression, update its status or remove it from the table

## Running the Validation Script

```bash
# From the repository root
./tests/run_all_tests.sh
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
