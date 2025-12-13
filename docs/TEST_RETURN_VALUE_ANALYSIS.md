# Test Return Value Analysis

This document tracks issues found in return value handling during testing.

## Summary of Issues Found

### 1. Sign Extension Issue - Return Values Getting Truncated

**Affected Tests:**
- `test_const_member_return.cpp` - Returns 255 instead of -1
- `test_member_return_simpleordering.cpp` - Returns 255 instead of -1  
- `test_return_simpleordering.cpp` - Returns 255 instead of -1

**Root Cause:**
When returning a struct containing a signed integer field (like `SimpleOrdering.value = -1`), the value gets truncated to unsigned byte (255 instead of -1). This suggests the return value in EAX is not being sign-extended properly.

**Expected Behavior:**
All three tests should return -1 (exit code 255 when interpreted as unsigned, but the actual value should be preserved as -1).

**Current Behavior:**
Returns 255 (0xFF) instead of -1, indicating lack of sign extension.

### 2. Compiler Crashes

**Affected Tests:**
- `test_covariant_return.cpp` - Segmentation fault during compilation
- `test_lambda_mismatched_returns_fail.cpp` - Segmentation fault during compilation  
- `test_mismatch_return_fail.cpp` - Should fail compilation with error message, currently crashes

**Expected Behavior:**
- `test_covariant_return.cpp` should compile successfully (tests covariant virtual return types)
- `test_lambda_mismatched_returns_fail.cpp` should fail compilation with proper error message
- `test_mismatch_return_fail.cpp` should fail compilation with proper error message

**Current Behavior:**
All three cause segmentation faults in the compiler.

## Test Results Summary

### Passing Tests
- `concept_return_type_req.cpp` (exit=0) ✓
- `if_only_return.cpp` (exit=1) ✓
- `if_then_return.cpp` (exit=0) ✓
- `just_return.cpp` (exit=0) ✓
- `template_return_only.cpp` (exit=42) ✓
- `template_void_return.cpp` (exit=0) ✓
- `test_func_return_local.cpp` (exit=8) ✓
- `test_param_passing_return.cpp` (exit=0) ✓
- `test_template_return_type.cpp` (exit=0) ✓
- `test_ternary_return.cpp` (exit=2) ✓
- `test_trailing_return.cpp` (exit=35) ✓

### Failing Tests (Return Value Issues)
- `test_const_member_return.cpp` - Returns 255 instead of -1 ✗
- `test_member_return_simpleordering.cpp` - Returns 255 instead of -1 ✗
- `test_return_simpleordering.cpp` - Returns 255 instead of -1 ✗

### Crashing Tests
- `test_covariant_return.cpp` - Compiler crashes ✗
- `test_lambda_mismatched_returns_fail.cpp` - Compiler crashes ✗
- `test_mismatch_return_fail.cpp` - Compiler crashes ✗

## Priority for Fixes

1. **High Priority:** Fix the sign extension issue for struct member returns (affects 3 tests)
2. **Medium Priority:** Fix covariant return type crash (complex feature)
3. **Low Priority:** Fix lambda/function mismatched return type crashes (these are fail tests, should emit errors not crash)

## Investigation Notes

### Sign Extension Issue Details

All three failing tests follow the same pattern:
```cpp
struct SimpleOrdering {
    int value;
    SimpleOrdering(int v) : value(v) {}
};

// Function returns SimpleOrdering(-1)
// main() extracts result.value and returns it
// Expected: -1 (as signed int)
// Actual: 255 (0xFF)
```

The issue is likely in how struct members are being loaded and returned. The value -1 is being stored correctly but when loaded from the struct and placed in EAX for the return, it's not being sign-extended from 32-bit to match the calling convention's expectations.

### Crash Investigation

Need to run under debugger or with verbose logging to understand where the crashes occur:
- Covariant returns involve vtable and inheritance complexity
- Lambda type deduction failures should be caught and reported as errors
- Mismatched return type detection should happen during parsing/type checking
