# Test Return Value Analysis

## Current Status (2025-12-20 - ArrayElementAddress Fix)

**641/661 tests passing (97.0%)**
- 11 runtime crashes (down from 12-14)
- 1 timeout (infinite loop)
- 2 link failures
- 2 known issues with workarounds documented below

**Run validation:** `cd /home/runner/work/FlashCpp/FlashCpp && ./tests/validate_return_values.sh`

## Key Note on Return Values

On Unix/Linux, `main()` return values are masked to 0-255 (8-bit). Values >255 are truncated via `value & 0xFF`.
- Returning 300 → exit code 44
- Returning 3000 → exit code 184
- **This is expected OS behavior, not a compiler bug**

## Recent Fixes (2025-12-20)

**ArrayElementAddress StringHandle Index Bug** ✅ FIXED
- **Issue**: Array element address calculation (`&arr[i].x`) crashed when index was a variable name
- **Root Cause**: handleArrayElementAddress only handled constant (unsigned long long) and TempVar indices
  - When index was StringHandle (variable name like "i"), neither case matched
  - No code generated → RAX contained stale value (0) → crash or wrong result
- **Fix**: Added StringHandle case in handleArrayElementAddress to:
  - Look up variable name in scope
  - Load index value from stack
  - Generate proper address calculation (multiply by element size, add to base)
- **Status**: ✅ COMPLETE
- **Tests Fixed**: `&arr[i].x` patterns now work when i is a variable

**AddressOf Member Access** ✅ PARTIAL FIX
- **Issue**: Taking address of struct members (`&obj.member`) generated incorrect IR
- **Root Cause**: IR generated `member_access` (loads VALUE) followed by `addressof` (takes address of temp)
- **Fix**: Added `AddressOfMember` IR opcode that directly computes member address using LEA
  - Generates: `LEA result, [RBP + obj_offset + member_offset]`
  - Handles simple identifier cases: `&obj.member` where obj is a variable name
  - Does NOT mark result as reference (avoids dereference issues in pointer arithmetic)
- **Status**: ✅ COMPLETE for simple cases
- **Tests Fixed**: Simple `&obj.member` cases, works in combination with ArrayElementAddress fix

**Lambda Decay to Function Pointer**
- **Issue**: Lambda expressions with unary plus operator (+lambda) crashed due to uninitialized function pointer
- **Root Cause**: Unary plus on non-capturing lambdas should trigger decay to function pointer (returning address of `__invoke` static function), but was being treated as a no-op, returning the closure object instead
- **Fix**: Modified `generateUnaryOperatorIr` in CodeGen.h to detect lambda expressions as operand of unary plus:
  - Check if operand is `LambdaExpressionNode` before visiting
  - For non-capturing lambdas, generate `FunctionAddress` IR for the `__invoke` function
  - Return function pointer (Type::FunctionPointer, 64 bits) instead of closure struct
  - Capturing lambdas fall through to normal handling (cannot decay to function pointers)
- **Status**: ✅ COMPLETE
- **Tests Fixed (1)**:
  - test_lambda_decay.cpp ✓ returns 0 (lambda decay with unary +)
- **Note**: test_lambda_cpp20_comprehensive.cpp still crashes (different lambda-related issue with captures)

## Past Fixes Summary

**Float Literal Init & Buffer Overflow** (8 tests) - Fixed OpCodeWithSize buffer (8→9 bytes), direct memory stores for floats  
**Range-For Loop Increment** (4 tests) - Fixed pointer increment to use element size not pointer size  
**AddressOf Array Elements** (1 test) - Fixed `&arr[i]` offsets for pointer/struct arrays  
**Arrays of Pointers Type** - Fixed type checking to distinguish arrays of pointers from pointer-to-array  

<details>
<summary><strong>All Completed Fixes (click to expand)</strong></summary>

- ✅ **Lambda decay to function pointer** (2025-12-20) - Fixed unary plus on lambdas to return __invoke address (1 test)
- ✅ **Float literal initialization** (2025-12-20) - Fixed buffer overflow and initialization (8 tests)
- ✅ **Range-based for loop pointer increment** (2025-12-20) - Fixed pointer increment to use correct element size (4 tests)
- ✅ **Array element size in AddressOf** (2025-12-20) - Fixed &arr[i] offset calculations (1 test)
- ✅ **Arrays of pointers flagged as pointer-to-array** (2025-12-20) - Correct array access (1 test)
- ✅ **Pointer variable size** (previous) - Fixed pointer identifiers to use 64-bit pointer size (4 tests)
- ✅ **Pointer member access type checking** (previous) - Allow pointers to structs in member access
- ✅ **Pure virtual functions** - Vtable entries use `__cxa_pure_virtual` for abstract classes
- ✅ **sizeof for struct arrays** - Fixed division by zero calculating sizeof(array_of_structs)
- ✅ **Stack alignment** (6 tests) - Fixed floating-point crashes with printf
- ✅ **Function pointers** (2 tests) - Global and pointer parameter function pointer calls  
- ✅ **Dereference register corruption** (1 test) - Clear stale register associations
- ✅ **Pointer member size** (7 tests) - Pointers/references in structs use correct 64-bit size
- ✅ **Temp variable stack allocation** (23 tests) - Fixed handleMemberAccess offset handling
- ✅ **Heap allocation constructor** - Fixed LEA vs MOV for heap vs stack objects
- ✅ **Multi-level pointer dereference** - Fixed type_index vs pointer_depth issue

</details>

## Known Issues

### Struct Padding/Alignment - RESOLVED ✅
**UPDATE**: Investigation revealed struct padding IS working correctly. The crashes attributed to this were actually caused by array element size bugs (now fixed). FlashCpp correctly:
- Calculates struct sizes with proper padding
- Aligns members based on their types
- Returns correct sizeof() values

Tests like test_pointer_arithmetic.cpp now pass after fixing the actual root causes.

### Float-to-Int Conversion in Assignments - Known Issue
**Issue**: Assignments from float/double to int variables don't generate FloatToInt IR conversion instructions, resulting in incorrect behavior (bit pattern is copied instead of being converted).
**Impact**: Tests that return converted float values may return incorrect results (but don't crash).
**Status**: Pre-existing issue, not introduced by recent fixes. Identified during float register spilling investigation.

### Default-Initialized Struct Array with AddressOf - Known Issue
**Issue**: Taking address of member in default-initialized struct array returns wrong value when dereferenced.
**Test File**: `test_struct_default_init_addressof.cpp`
**Example**:
```cpp
struct S {
    int x{10};
    int* p = nullptr;
};

int main() {
    S arr[3]{};
    int i = 1;
    arr[0].p = &arr[i].x;
    return *arr[0].p;  // Expected: 10, Actual: 64
}
```
**Root Cause**: Appears to be related to constructor execution or struct initialization, NOT address calculation.
- Address offset calculation is correct (verified in assembly: `imul $0x10,%rcx` for 16-byte struct)
- The fixes for ArrayElementAddress (StringHandle support, element size correction) are working correctly
- The issue manifests when dereferencing the stored pointer
**Impact**: Returns garbage value (64 or similar) instead of expected member value (10).
**Status**: Separate issue from AddressOf/ArrayElementAddress bugs. Needs investigation into:
- Default initialization with `{}` for struct arrays
- Constructor call sequencing
- Memory initialization patterns
**Note**: Simple assignment without default init works correctly (returns 20 when explicitly set).

### Nested Member Access with AddressOf - Known Limitation
**Issue**: Multi-level member access like `&arr[i].member1.member2` doesn't work correctly.
**Test Cases**:
- `arr[i].inner.value` compiles but returns wrong value (46 instead of 20)
- `arr[i].inner_arr[j].value` causes compiler to hang (infinite loop or stack overflow)
**Root Cause**: Current AddressOf fixes handle single-level nesting (`&arr[i].member`) but not chained member access.
**What Works**: 
- ✅ `&obj.member` (simple member)
- ✅ `&arr[i].member` (single-level: array element + member)
**What Doesn't Work**:
- ❌ `&arr[i].member1.member2` (multi-level member chain)
- ❌ `&arr[i].inner_arr[j].member` (nested array subscripts)
**Impact**: 
- Single-level cases work correctly after fixes
- Multi-level cases need additional work to properly chain offset calculations
**Status**: Known limitation, separate from the StringHandle and element size bugs that were fixed.
**Future Work**: Extend IR generation to handle chained member access by accumulating offsets through multiple levels.

## Remaining Crashes (12 files + 1 timeout)

**Current: 12 crashes, 1 timeout** (down from 13 - lambda decay now fixed!)

### Crash Categories

1. **Lambda** (1 file) - Capture-related issues  
   test_lambda_cpp20_comprehensive.cpp (test_lambda_decay.cpp ✓ FIXED)

2. **Exceptions** (2 files) - Incomplete Linux exception support  
   test_exceptions_basic.cpp, test_exceptions_nested.cpp

3. **Template specialization** (2 files)  
   test_spec_member_only.cpp, test_specialization_member_func.cpp

4. **Variadic arguments** (2 files)  
   test_va_implementation.cpp, test_varargs.cpp

5. **Other issues** (5 files)  
   - spaceship_default.cpp (SIGILL - C++20 three-way comparison)
   - test_pointer_loop.cpp (member access through pointer in loop)
   - test_rvo_very_large_struct.cpp (large struct RVO)
   - test_template_complex_substitution.cpp (complex template)

6. **Link failures** (2 files)
   - test_abstract_class.cpp (vtable/typeinfo relocation)
   - test_covariant_return.cpp (covariant return types)

7. **Timeout** (1 file) - test_xvalue_all_casts.cpp

**Fixed**: Lambda decay (test_lambda_decay.cpp), plus all floating-point register spilling tests now pass!

## Priority Investigation Areas

1. **Lambda capture** - 1 test with complex lambda captures (test_lambda_cpp20_comprehensive.cpp)
2. **Template specialization** - 2 tests with member function specialization
3. **Exception handling** - 2 tests requiring complete Linux exception support
4. **Variadic arguments** - 2 tests with va_list implementation issues
5. **Three-way comparison** - 1 test with C++20 spaceship operator

---

*Last Updated: 2025-12-20 (after lambda decay fix)*
*Status: 640/661 tests passing (96.8%), 12 crashes, 1 timeout, 2 link failures*
*Run validation: `cd /home/runner/work/FlashCpp/FlashCpp && ./tests/validate_return_values.sh`*
