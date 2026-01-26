# Expected Return Values for FlashCpp Tests

This document tracks the expected return values for test files and documents any regressions found during validation.

## Overview

Many test files in the `tests/` directory follow the naming convention `test_name_retNN.cpp` where `NN` is the expected return value from the `main()` function. The `validate_return_values.sh` script validates that tests return their expected values.

## Validation Summary

**Last Run:** 2026-01-26 (validate_return_values.sh)

**Total files tested:** 959
**Valid returns:** 955
**Regressions (mismatches):** 7
**Runtime crashes:** 2
**Compile failures:** 0
**Link failures:** 0

## Current Regressions

The following tests are returning incorrect values:

| Test File | Expected | Currently Returning | Notes |
|-----------|----------|---------------------|-------|
| test_less_in_base_class_ret0.cpp | 0 | 1 | Test expects 0 but returns 1 |
| test_member_func_trailing_requires_ret42.cpp | 42 | 28 | Member function with trailing requires clause |
| test_member_partial_spec_inherit_ret4.cpp | 4 | 0 | Partial specialization inheritance issue |
| test_member_var_template_ret42.cpp | 42 | 0 | Variable template member access |
| test_out_of_line_ctor_ret0.cpp | 0 | 3 | Out-of-line constructor definition |
| test_qualified_base_class_ret42.cpp | 42 | 0 | Qualified base class access |
| test_template_disambiguation_pack_ret40.cpp | 40 | 30 | Template disambiguation with parameter pack |

## Runtime Crashes

The following test files crash at runtime:

1. **test_exceptions_nested.cpp** - Signal 6 (Abort)
   - Nested exception handling test
   - Crashes during exception throwing/catching
   - Represents missing or incomplete exception handling in the compiler

2. **test_placement_new_parsing_ret42.cpp** - Signal 11 (Segmentation fault)
   - Tests placement new with multiple arguments
   - Uses alignas and array placement new features
   - Crash likely related to unsupported placement new syntax or alignas

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
