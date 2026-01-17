# Expected Return Values for FlashCpp Tests

This document tracks the expected return values for test files and documents any regressions found during validation.

## Overview

Many test files in the `tests/` directory follow the naming convention `test_name_retNN.cpp` where `NN` is the expected return value from the `main()` function. The `validate_return_values.sh` script validates that tests return their expected values.

## Validation Summary

**Last Run:** 2026-01-17 (current validation run)

**Total files tested:** 934
**Valid returns:** 934
**Regressions (mismatches):** 0
**Runtime crashes:** 0
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
| test_void_t_positive_ret0.cpp | 0 | 42 | void_t SFINAE fix | Template aliases like void_t that resolve to concrete types now correctly detected during pattern matching |
| test_template_disambiguation_pack_ret40.cpp | 40 | 20→30→40 | Type template arg mangling | Function template specializations now include type template args in mangled names (sum<int> → `_ZN2ns3sumIiEEv`, sum<int,int> → `_ZN2ns3sumIiiEEv`) |
| test_qualified_base_class_ret42.cpp | 42 | 0 | Global namespace fix | Fixed by global namespace symbol lookup improvements |
| test_sizeof_template_param_default_ret4.cpp | 4 | 1 | Global namespace fix | Fixed as side effect of namespace lookup improvements |
| test_std_header_features_ret0.cpp | 0 | 8 | Global namespace fix | Fixed as side effect of namespace lookup improvements |
| test_global_namespace_scope_ret1.cpp | 1 (1025%256) | 145 | Using declaration fix | Fixed by tracking qualified names when resolving using declarations in CodeGen |
| test_global_scope_new_ret0.cpp | 0 | 1 | Scalar new initialization | Store scalar initializer values for `new T(args)` |
| test_spec_nullptr_init_ret0.cpp | 0 | 214 | Template default constructor | Generate implicit default constructor for partial specializations so member initializers run |

## Regressions Found

No regressions found in the latest validation run.

## Root Cause Summary

No open regressions in the current validation run.

## Runtime Crashes

No runtime crashes observed in the latest validation run.

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
