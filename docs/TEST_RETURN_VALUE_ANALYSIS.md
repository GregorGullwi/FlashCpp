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
- ~~`test_covariant_return.cpp` - Compiler hangs (infinite loop in parsing/codegen)~~ **PARTIALLY FIXED** - Added main function and fixed infinite loop, but compiler now fails with error due to symbol table issue (see Fix 2 below) ⚠️

### Correctly Failing Tests (Expected to Fail Compilation)
- `test_lambda_mismatched_returns_fail.cpp` - Correctly reports error: "Lambda has inconsistent return types" ✓
- `test_mismatch_return_fail.cpp` - Correctly reports error: "Return type mismatch in out-of-line definition" ✓

## Priority for Fixes

1. **~~High Priority:~~ COMPLETED** - ~~Fix the sign extension issue for struct member returns (affects 3 tests)~~ Fixed 32-bit immediate loading optimization
2. **~~Medium Priority:~~ PARTIALLY COMPLETED** - ~~Investigate covariant return type hang~~ Fixed infinite loop, but symbol table issue remains
3. **~~Medium Priority:~~ COMPLETED** - ~~Fix lambda/function mismatched return type crashes~~ These tests are working correctly - they properly detect and report errors
4. **Medium Priority:** Fix pointer member access symbol table lookup issue (affects any test using `ptr->member` pattern)

## Investigation Notes

### Covariant Return Type Investigation (December 2024)

**Problem:** The file `test_covariant_return.cpp` was missing a `main()` function and caused the compiler to hang indefinitely.

**Root Causes Found:**
1. Missing `main()` function - test file had only helper functions
2. Infinite loop in `visitReturnStatementNode` when expression evaluation fails
3. Missing `MemberAccessNode` handling in `get_expression_type()`  
4. Symbol table lookup failure for local pointer variables in member access expressions

**Investigation Process:**
Through systematic testing with progressively simpler test cases, discovered the hang occurs specifically when:
- Using `->` operator on a pointer to access struct members
- The pointer is returned from a function (even non-virtual functions)
- The member access result is used in a return statement

Example failing code:
```cpp
Dog* ptr = d.getSelf();  // Works fine
return ptr->breed;        // Causes hang/error
```

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

### Fix 2: Prevent Infinite Loop on Return Statement Expression Evaluation Failure (Commit a08cccf)

**Problem:** When `visitExpressionNode` returns an empty vector due to an error (e.g., symbol not found), the code in `visitReturnStatementNode` tries to access `operands[2]` without bounds checking. This causes undefined behavior that manifested as an infinite loop or crash.

**Solution:** Added bounds checking before accessing the operands array:
```cpp
// Check if operands is non-empty before accessing
if (operands.empty()) {
    FLASH_LOG(Codegen, Error, "Return statement: expression evaluation failed");
    return;
}
```

**Code Changes:**
- Added bounds check in `src/CodeGen.h` at line ~2275 in `visitReturnStatementNode`
- Added `MemberAccessNode` case in `src/Parser.cpp` in `get_expression_type()` function to properly handle member access expressions

**Impact:** Compiler now fails gracefully with a clear error message instead of hanging indefinitely. This makes debugging much easier and prevents the compiler from appearing to freeze.

### Fix 3: Added MemberAccessNode Type Deduction (Commit a08cccf)

**Problem:** The `get_expression_type()` function in Parser.cpp didn't handle `MemberAccessNode`, causing it to return `std::nullopt` for expressions like `ptr->member`. This contributed to type resolution failures.

**Solution:** Added case to handle `MemberAccessNode` by:
1. Getting the type of the object being accessed
2. Looking up the struct type information
3. Finding the member in the struct
4. Returning the member's type

**Code Changes:**
- Added `MemberAccessNode` handling in `get_expression_type()` in `src/Parser.cpp`

**Impact:** Improves type deduction for member access expressions, though the symbol table lookup issue in codegen still needs to be resolved.

## Remaining Issues

### Pointer Member Access Symbol Table Lookup Failure

**File:** `src/CodeGen.h`, `generateMemberAccessIr` (around line 9644)

**Problem:** When generating code for `ptr->member` where `ptr` is a local variable, the code path for handling pointer dereference requires the operand to be an `IdentifierNode` and looks it up in the symbol table. However, the lookup fails even for variables declared in the same function.

**Current Behavior:** Compiler logs error "pointer 'ptr' not found in symbol table" and fails gracefully.

**Possible Root Causes:**
1. Symbol table timing issue - symbols might be looked up before they're inserted
2. The code is too restrictive - it only handles `IdentifierNode` operands, but should support any expression that evaluates to a pointer (e.g., function calls like `getPointer()->member`)

**Recommended Fix:** Modify `generateMemberAccessIr` to evaluate the pointer expression using `visitExpressionNode()` instead of requiring it to be a simple identifier. This would:
- Support complex pointer expressions (function calls, nested member access, etc.)
- Avoid direct symbol table lookups that may have timing issues
- Be consistent with how other expression types are handled in the codebase

**Test Cases Affected:**
- `test_covariant_return.cpp` - Now has main function but fails due to this issue
- Any code using pattern: `ptr->member` where ptr is a local variable

**Workaround:** Use dereference and dot operator instead: `(*ptr).member`
