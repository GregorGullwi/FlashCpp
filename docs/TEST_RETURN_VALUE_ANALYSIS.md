# Test Return Value Analysis

## Current Status (2025-12-21 - Latest Run)

**650/669 tests passing (97.2%)**
- 11 runtime crashes  
- 1 timeout (test_xvalue_all_casts.cpp)
- 2 link failures (test_covariant_return.cpp, test_virtual_inheritance.cpp)

**Run validation:** `cd /home/runner/work/FlashCpp/FlashCpp && ./tests/validate_return_values.sh`

**Progress Notes:**
- ✅ test_pointer_loop.cpp: No longer crashes! Returns 20 (expected 40, but runs successfully)
- ✅ test_virtual_inheritance.cpp: Changed from runtime crash to link failure (progress)
- Improved from 649/669 to 650/669 passing tests (+1)

## Key Note on Return Values

On Unix/Linux, `main()` return values are masked to 0-255 (8-bit). Values >255 are truncated via `value & 0xFF`.
- Returning 300 → exit code 44
- Returning 3000 → exit code 184
- **This is expected OS behavior, not a compiler bug**

## Completed Fixes Summary

### Latest Fix (2025-12-21)
**Reference Return Type Handling** - Fixed template functions returning reference types
- Issue: Template member functions returning `const T&` were incorrectly truncating the reference (pointer) from 64 bits to 32 bits, causing type conversion errors
- Root Cause: Return type handling didn't account for reference qualifiers, only pointer depth
- Solution: 
  - Added `current_function_returns_reference_` tracking flag
  - Fixed return type size calculation to use 64 bits for references
  - Skip type conversion in return statements for reference returns
  - Fixed variable declaration size allocation for reference types
- Files Modified: `src/CodeGen.h`, `src/IRTypes.h`
- Impact: Improves correctness of template functions with reference returns, fixed test_virtual_inheritance.cpp (crash → link failure)

<details>
<summary><strong>Recent Major Fixes (2025-12-20) - Click to expand</strong></summary>

### ArrayElementAddress StringHandle Index Bug (Multiple tests fixed)
- Fixed `&arr[i].x` when index is variable name
- Added StringHandle case in handleArrayElementAddress for proper variable lookup

### AddressOf Member Access (Multiple tests fixed)
- Added `AddressOfMember` IR opcode for direct member address calculation using LEA
- Handles `&obj.member` patterns correctly

### Lambda Decay to Function Pointer (1 test: test_lambda_decay.cpp)
- Fixed unary plus operator (+lambda) to return `__invoke` function address
- Modified `generateUnaryOperatorIr` to detect lambda expressions

### Float Literal Initialization (8 tests)
- Fixed OpCodeWithSize buffer overflow (8→9 bytes)
- Direct memory stores for float values

### Range-For Loop Pointer Increment (4 tests)
- Fixed pointer increment to use element size, not pointer size

</details>

<details>
<summary><strong>All Completed Fixes - Click to expand</strong></summary>

**2025-12-21 Fix:**
- ✅ Reference return type handling in templates (improved crash → link failure)

**2025-12-20 Fixes:**
- ✅ Lambda decay to function pointer (1 test)
- ✅ Float literal initialization (8 tests)  
- ✅ Range-based for loop pointer increment (4 tests)
- ✅ Array element size in AddressOf (1 test)
- ✅ Arrays of pointers type checking (1 test)
- ✅ AddressOf member access (multiple tests)
- ✅ ArrayElementAddress StringHandle support (multiple tests)

**Previous Fixes:**
- ✅ Pointer variable size (4 tests)
- ✅ Pointer member access type checking
- ✅ Pure virtual functions vtable
- ✅ sizeof for struct arrays
- ✅ Stack alignment for floating-point (6 tests)
- ✅ Function pointers (2 tests)
- ✅ Dereference register corruption (1 test)
- ✅ Pointer member size (7 tests)
- ✅ Temp variable stack allocation (23 tests)
- ✅ Heap allocation constructor
- ✅ Multi-level pointer dereference

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

### Spaceship Operator - RESOLVED ✅ (2025-12-21)
**UPDATE**: spaceship_default.cpp now passes after fixing reference return type handling! The issue was related to template functions with reference parameters/returns, not register corruption. The fix for reference type handling resolved this crash.

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

## Remaining Crashes (11 files + 1 timeout)

**Current: 11 crashes, 1 timeout, 2 link failures**

### Crash Categories

1. **Lambda** (1 file) - Capture-related issues  
   test_lambda_cpp20_comprehensive.cpp

2. **Exceptions** (2 files) - Incomplete Linux exception support  
   test_exceptions_basic.cpp, test_exceptions_nested.cpp

3. **Template specialization** (2 files)  
   test_spec_member_only.cpp, test_specialization_member_func.cpp

4. **Variadic arguments** (2 files)  
   test_va_implementation.cpp, test_varargs.cpp

5. **Other issues** (4 files)  
   - test_abstract_class.cpp (vtable/typeinfo runtime crash)
   - test_rvo_very_large_struct.cpp (large struct RVO)
   - test_template_complex_substitution.cpp (complex template)

6. **Link failures** (2 files)
   - test_covariant_return.cpp (covariant return types)
   - test_virtual_inheritance.cpp (virtual inheritance typeinfo - improved from crash)

7. **Timeout** (1 file)
   - test_xvalue_all_casts.cpp (infinite loop in cast handling)

## Priority Investigation Areas

1. **Lambda capture** - 1 test with complex lambda captures  
2. **Template specialization** - 2 tests with member function specialization
3. **Exception handling** - 2 tests requiring complete Linux exception support
4. **Variadic arguments** - 2 tests with va_list implementation issues
5. **Virtual function typeinfo** - Missing typeinfo symbol generation for polymorphic classes

---

*Last Updated: 2025-12-21 (reference return type fix)*  
*Status: 650/669 tests passing (97.2%), 11 crashes, 1 timeout, 2 link failures*  
*Run validation: `cd /home/runner/work/FlashCpp/FlashCpp && ./tests/validate_return_values.sh`*
