# Expected Return Values for FlashCpp Tests

This document tracks the expected return values for test files and documents any return mismatches found during validation.

## Overview

Many test files in the `tests/` directory follow the naming convention `test_name_retNN.cpp` where `NN` is the expected return value from the `main()` function. The `validate_return_values.sh` script validates that tests return their expected values.

## Validation Summary

**Last Run:** 2026-01-27 (validate_return_values.sh - after template function explicit args fix + validation script enhancements)

**Total files tested:** 960
**Valid returns:** 952
**Return mismatches:** 2
**Runtime crashes:** 2
**Compile failures:** 1
**Link failures:** 1

## Known Return Mismatches

The following tests have return value mismatches. These are tests where the filename indicates an expected return value (e.g., `_ret42.cpp` expects 42), but the actual return value differs:

| Test File | Expected | Actually Returns | Notes |
|-----------|----------|------------------|-------|
| test_var_template_static_inline_ret132.cpp | 132 | 99 | Variable template with static inline member - value calculation issue |
| test_var_template_values_ret162.cpp | 162 | 47 | Variable template value instantiation - value calculation issue |

**Note:** These are legitimate bugs in the compiler where the variable template instantiation or evaluation is not working correctly. They are tracked separately from the previously fixed template function call issues.

**Fixed in previous PRs:**
- ~~test_less_in_base_class_ret0.cpp~~ - **FIXED**: Template base class with comparison expressions in template arguments now correctly evaluated
- ~~test_out_of_line_ctor_ret0.cpp~~ - **FIXED**: Copy constructor now correctly preferred over converting constructors for direct initialization
- ~~test_member_func_trailing_requires_ret42.cpp~~ - **FIXED**: sizeof(T) in template member functions with trailing requires clauses now correctly evaluates template parameter
- ~~test_qualified_base_class_ret42.cpp~~ - **FIXED**: Template base classes with qualified member type access (e.g., Base<T>::type) now correctly resolved during instantiation
- ~~test_member_var_template_ret42.cpp~~ - **FIXED**: Member variable templates with non-type parameters and sizeof expressions now correctly instantiate and evaluate
- ~~test_member_partial_spec_inherit_ret4.cpp~~ - **FIXED**: Partial specialization static member with sizeof expression now correctly evaluates template parameters
- ~~test_template_disambiguation_pack_ret40.cpp~~ - **FIXED**: Function template calls with explicit template arguments now correctly resolve to the matching specialization based on template argument count

## Compile Failures

The following test files fail to compile:

1. **test_std_move_support.cpp** - Compilation error
   - Tests std::move and related features
   - Represents missing standard library support or incomplete move semantics implementation

## Link Failures

The following test files fail to link:

1. **test_rvo_cannot_apply.cpp** - Linking error
   - Tests return value optimization (RVO) edge cases
   - Represents missing implementation or unresolved symbols

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
