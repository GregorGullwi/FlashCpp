# Test Return Value Analysis

## Current Status (2025-12-21 - Latest Run)

**649/669 tests passing (97.0%)**
- 12 runtime crashes  
- 1 timeout (test_xvalue_all_casts.cpp)
- 1 link failure (test_covariant_return.cpp)

**Run validation:** `cd /home/runner/work/FlashCpp/FlashCpp && ./tests/validate_return_values.sh`

**Progress Notes:**
- test_pointer_loop.cpp: ✓ No longer crashes! Returns 20 (expected 40, but runs successfully)
- Improved from 641/661 to 649/669 passing tests

## Key Note on Return Values

On Unix/Linux, `main()` return values are masked to 0-255 (8-bit). Values >255 are truncated via `value & 0xFF`.
- Returning 300 → exit code 44
- Returning 3000 → exit code 184
- **This is expected OS behavior, not a compiler bug**

## Completed Fixes Summary

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

### Spaceship Operator with Multiple Comparisons - Active Investigation 🔍
**Issue**: Using `operator<=>` with multiple synthesized comparison operators causes SIGILL or segfault when combined with nested struct member access.
**Test File**: `spaceship_default.cpp`
**Trigger Conditions**:
1. Struct with `auto operator<=>(const T&) const = default`
2. Using 3+ synthesized comparison operators (==, !=, <, >, <=, >=)
3. Another struct containing a member of type with operator<=>
4. Accessing nested member (e.g., `obj.member.value`)

**Minimal Reproduction**:
```cpp
struct Point {
    int x, y;
    auto operator<=>(const Point&) const = default;
};
struct Inner {
    int value;
    auto operator<=>(const Inner&) const = default;
};
struct Outer { Inner member; };

int main() {
    Point p1{1, 2}, p2{1, 3};
    bool eq = p1 == p2;
    bool ne = p1 != p2;
    bool lt = p1 < p2;  // 3rd comparison triggers it
    
    Outer o1;
    o1.member.value = 10;  // <-- SIGILL or segfault here
    return 0;
}
```

**Observed Behavior**:
- 1-2 comparisons: Works fine
- 3-4 comparisons + member access: Segmentation fault (signal 11)
- 5-6 comparisons + member access: Illegal instruction (signal 4 - SIGILL)
- Bad instruction at offset 0x8ab: `48 c7 0a 00 ...` (malformed MOV instruction)

**Root Cause**: Likely register exhaustion or corruption in code generation after synthesizing multiple comparison operators. The comparison operators use registers R8-R15, and after ~3 comparisons, register state tracking appears corrupted, generating invalid x86-64 instructions.

**Impact**: Prevents use of C++20 three-way comparison with comprehensive operator usage
**Status**: Requires fix in register allocation/tracking in IRConverter.h code generation
**Workaround**: Limit to 2 or fewer comparison operators, or avoid nested member access after comparisons

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

**Current: 12 crashes, 1 timeout, 1 link failure**

### Crash Categories

1. **Lambda** (1 file) - Capture-related issues  
   test_lambda_cpp20_comprehensive.cpp

2. **Exceptions** (2 files) - Incomplete Linux exception support  
   test_exceptions_basic.cpp, test_exceptions_nested.cpp

3. **Template specialization** (2 files)  
   test_spec_member_only.cpp, test_specialization_member_func.cpp

4. **Variadic arguments** (2 files)  
   test_va_implementation.cpp, test_varargs.cpp

5. **Spaceship operator** (1 file) - Register corruption with multiple comparisons  
   spaceship_default.cpp (SIGILL - see detailed analysis in Known Issues section)

6. **Other issues** (4 files)  
   - test_abstract_class.cpp (vtable/typeinfo runtime crash)
   - test_rvo_very_large_struct.cpp (large struct RVO)
   - test_template_complex_substitution.cpp (complex template)

7. **Link failures** (1 file)
   - test_covariant_return.cpp (covariant return types)

8. **Timeout** (1 file)
   - test_xvalue_all_casts.cpp (infinite loop in cast handling)

**Progress**: test_pointer_loop.cpp ✓ NO LONGER CRASHES (returns wrong value but runs)

## Priority Investigation Areas

1. **Spaceship operator register corruption** - 1 test (spaceship_default.cpp) - detailed analysis complete
2. **Lambda capture** - 1 test with complex lambda captures  
3. **Template specialization** - 2 tests with member function specialization
4. **Exception handling** - 2 tests requiring complete Linux exception support
5. **Variadic arguments** - 2 tests with va_list implementation issues

---

*Last Updated: 2025-12-21 (spaceship operator investigation)*  
*Status: 649/669 tests passing (97.0%), 12 crashes, 1 timeout, 1 link failure*  
*Run validation: `cd /home/runner/work/FlashCpp/FlashCpp && ./tests/validate_return_values.sh`*
