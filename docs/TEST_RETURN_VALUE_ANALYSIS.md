# Test Return Value Analysis

This document tracks issues found in return value handling during testing.

## Summary of Issues Found

### 1. ~~Sign Extension Issue - Return Values Getting Truncated~~ (RESOLVED - NOT A BUG)

**Affected Tests:**
- `test_const_member_return.cpp` - Returns 255 instead of -1
- `test_member_return_simpleordering.cpp` - Returns 255 instead of -1  
- `test_return_simpleordering.cpp` - Returns 255 instead of -1

**Resolution:**
These tests are actually working CORRECTLY. Exit codes in Unix/Linux are 8-bit unsigned values (0-255). When a program returns -1 as a signed 32-bit integer, the operating system truncates it to 8 bits, resulting in 255 (0xFF). This is the expected behavior and matches what clang/gcc produce.

**Fix Applied:**
Fixed the code generation for 32-bit integer immediate values in function calls. The compiler was using `movabs r64, imm64` (10-byte instruction) instead of the more efficient `mov r32, imm32` (5-byte instruction). This has been corrected to use `emitMovImm32()` for 32-bit arguments, which properly handles signed 32-bit values like -1 (0xFFFFFFFF).

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
- `test_const_member_return.cpp` (exit=255) ✓ (255 is correct for -1)
- `test_member_return_simpleordering.cpp` (exit=255) ✓ (255 is correct for -1)
- `test_return_simpleordering.cpp` (exit=255) ✓ (255 is correct for -1)

### Failing Tests (Return Value Issues)
- None! All return value tests are now passing.

### Crashing Tests
- `test_covariant_return.cpp` - Compiler hangs (infinite loop in parsing/codegen) ✗

### Correctly Failing Tests (Expected to Fail Compilation)
- `test_lambda_mismatched_returns_fail.cpp` - Correctly reports error: "Lambda has inconsistent return types" ✓
- `test_mismatch_return_fail.cpp` - Correctly reports error: "Return type mismatch in out-of-line definition" ✓

## Priority for Fixes

1. **~~High Priority:~~ COMPLETED** - ~~Fix the sign extension issue for struct member returns (affects 3 tests)~~ Fixed 32-bit immediate loading optimization
2. **Low Priority:** Investigate covariant return type hang (complex feature with 200 lines, causes infinite loop - likely edge case)
3. **~~Medium Priority:~~ COMPLETED** - ~~Fix lambda/function mismatched return type crashes~~ These tests are working correctly - they properly detect and report errors

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

## Fixes Applied

### Fix 1: 32-bit Immediate Loading Optimization (Commit 77ddcab)

**Problem:** When passing 32-bit integer constants as function arguments, the compiler was using the longer `movabs r64, imm64` instruction (10 bytes) instead of the shorter `mov r32, imm32` instruction (5 bytes).

**Solution:** Added `emitMovImm32()` function and updated `handleFunctionCall()` to check the argument size and use the appropriate instruction:
- For 32-bit arguments: use `mov r32, imm32` (5 bytes, zero-extends to 64-bit)
- For 64-bit arguments: use `mov r64, imm64` (10 bytes)

**Code Changes:**
- Added `emitMovImm32()` function in IRConverter.h (lines 4889-4902)
- Updated function argument loading logic (lines 5695-5706) to check `arg.size_in_bits` and call appropriate emit function

**Impact:** Generates more compact and efficient code for 32-bit integer arguments, matching what clang/gcc produce.
