# Test Return Value Analysis

## Current Status (2025-12-20 - Updated)

**635/661 tests passing (96.1%)**
- 18 runtime crashes (down from 21)
- 1 timeout (infinite loop)
- 2 link failures
- 7 compiler crashes (function pointer members - pre-existing issue)

**Run validation:** `cd /home/runner/work/FlashCpp/FlashCpp && ./tests/validate_return_values.sh`

## Key Note on Return Values

On Unix/Linux, `main()` return values are masked to 0-255 (8-bit). Values >255 are truncated via `value & 0xFF`.
- Returning 300 → exit code 44
- Returning 3000 → exit code 184
- **This is expected OS behavior, not a compiler bug**

## Recent Fixes (2025-12-20)

**Latest Fix: XMM Register Handling - ModR/M Byte and Buffer Overflow**
- **Issue**: Tests with >8 floating-point variables crashed with segmentation faults
- **Root Causes**: 
  1. ModR/M byte calculation used addition (`+`) instead of bitwise OR (`|`), causing incorrect addressing when XMM register bits were non-zero
  2. `MAX_MOV_INSTRUCTION_SIZE` was 8 bytes, but SSE instructions with REX prefix and 32-bit displacement need 9 bytes, causing buffer overflow
- **Fix**: 
  - Changed all ModR/M byte calculations from `+` to `|` in `generateFloatMovFromFrame()`, `generateFloatMovToFrame()`, `generateFloatMovFromMemory()`, and inline emission code (7 locations total)
  - Increased `MAX_MOV_INSTRUCTION_SIZE` from 8 to 16 bytes to accommodate complex SSE instructions
- **Status**: ✅ COMPLETE
- **Tests Fixed (3)**:
  - test_all_xmm_registers.cpp ✓ no longer crashes (16 float variables using all XMM registers)
  - test_float_register_spilling.cpp ✓ no longer crashes (20 float variables)
  - test_mixed_float_double_params.cpp ✓ no longer crashes (12 float/double parameters)
- **Note**: These tests currently return 0 instead of expected values due to a separate float-to-int conversion issue

**Previous Fix: Range-Based For Loop Pointer Increment**
- **Issue**: Range-based for loops crashed due to incorrect pointer increment size
- **Root Cause**: When creating begin/end pointers in `visitRangedForArray`, the code passed pointer size (64 bits) as `size_in_bits` to TypeSpecifierNode. When incrementing, `getSizeInBytes()` used this to calculate increment (64/8 = 8 bytes), ignoring actual element size
- **Fix**: Modified `visitRangedForArray` in CodeGen.h to calculate actual element size:
  - Regular arrays (e.g., `int arr[3]`): use base type size (32 bits for int → 4 byte increment)
  - Arrays of pointers (e.g., `int* arr[3]`): use pointer size (64 bits → 8 byte increment)
  - Arrays of structs: lookup size from gTypeInfo
- **Status**: ✅ COMPLETE
- **Tests Fixed (4)**:
  - test_range_for.cpp ✓ returns 15
  - test_range_for_simple.cpp ✓ returns 10
  - test_custom_container.cpp ✓ returns 15
  - test_range_for_begin_end.cpp, test_range_for_const_ref.cpp ✓ no longer crash (note: have pre-existing array store issues unrelated to range-for)

**Previous Fix: Array Element Size in AddressOf Operations**
- **Issue**: Taking address of array elements (`&arr[i]`) calculated wrong offsets for arrays of pointers and struct arrays
- **Root Cause**: AddressOf handler used identifier size (64 bits for arrays) as element size instead of actual element size
- **Fix**: Modified UnaryOperator AddressOf handler in CodeGen.h to properly calculate element size:
  - Regular arrays (e.g., `int arr[3]`): use base type size (32 bits for int)
  - Arrays of pointers (e.g., `int* arr[3]`): use pointer size (64 bits)
  - Arrays of structs: lookup size from gTypeInfo
- **Status**: ✅ COMPLETE
- **Tests Fixed (1)**:
  - test_pointer_arithmetic.cpp - Pointer arithmetic with arrays of pointers ✓ returns 20
  
**Previous Fix: Arrays of Pointers Incorrectly Flagged as Pointer-to-Array**
- **Issue**: Arrays of pointers (`int* arr[3]`) were treated as pointer variables instead of actual arrays
- **Root Cause**: Type checking at CodeGen.h:9735 set `is_pointer_to_array=true` when `pointer_depth() > 0`, even for arrays
- **Fix**: Added check `&& !(decl_ptr->is_array() || type_node.is_array())` to exclude arrays from pointer-to-array treatment
- **Status**: ✅ COMPLETE - ArrayStore operations now use correct direct stack access for arrays of pointers

<details>
<summary><strong>Investigation: Struct Padding (NOT the root cause)</strong></summary>

Initial investigation focused on struct padding as documented in Known Issues. However, testing revealed:
- FlashCpp correctly calculates struct padding and alignment
- `sizeof(P)` with mixed-size members returns correct value (e.g., 32 bytes for struct with int, char, float, double, int*)  
- Member offsets are correctly calculated with proper alignment
- The crashes were actually caused by array element size bugs (now fixed)

</details>

<details>
<summary><strong>Completed Fixes (click to expand)</strong></summary>

- ✅ **XMM register handling** (2025-12-20) - Fixed ModR/M byte encoding and buffer overflow (3 tests)
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

### Compiler Crashes (7 files - Pre-existing Issue)
Function pointer member tests cause compiler hangs/crashes:
- test_funcptr_call_noinit.cpp, test_funcptr_minimal.cpp, test_func_ptr_simple.cpp
- test_funcptr_global.cpp, test_funcptr_param.cpp, test_funcptr_member_init.cpp  
- test_func_ptr_struct_only.cpp, test_funcptr_noinit.cpp, test_member_init.cpp
- **Issue**: Compiler enters infinite loop when processing structs with function pointer members
- **Status**: Under investigation - not caused by recent changes

## Remaining Crashes (18 files + 1 timeout)

**Current: 18 crashes, 1 timeout** (down from 21 - XMM register handling now fixed)

### Crash Categories (Compacted)

1. **Floating-point** (2 files) - XMM register spilling, >8 float/double params  
   test_comprehensive_registers.cpp, test_register_spilling.cpp
   - **Fixed**: test_all_xmm_registers.cpp, test_float_register_spilling.cpp, test_mixed_float_double_params.cpp ✅

2. **Lambda** (2 files) - Capture and decay to function pointers  
   test_lambda_decay.cpp, test_lambda_cpp20_comprehensive.cpp

3. **Exceptions** (2 files) - Incomplete Linux exception support  
   test_exceptions_basic.cpp, test_exceptions_nested.cpp

4. **Template specialization** (2 files)  
   test_spec_member_only.cpp, test_specialization_member_func.cpp

5. **Variadic arguments** (2 files)  
   test_va_implementation.cpp, test_varargs.cpp

6. **Other issues** (8 files)  
   - spaceship_default.cpp (SIGILL - C++20 three-way comparison)
   - test_abstract_class.cpp (link failure - vtable/typeinfo relocation)
   - test_covariant_return.cpp (link failure)
   - test_pointer_loop.cpp (member access through pointer in loop)
   - test_rvo_very_large_struct.cpp, test_stack_overflow.cpp
   - test_template_complex_substitution.cpp, test_ten_mixed.cpp
   - test_virtual_inheritance.cpp

7. **Timeout** (1 file) - test_xvalue_all_casts.cpp

**Recently Fixed**:
- XMM register handling (3 tests): test_all_xmm_registers.cpp, test_float_register_spilling.cpp, test_mixed_float_double_params.cpp ✅
- Range-based for loops (4 tests): test_range_for.cpp, test_range_for_simple.cpp, test_custom_container.cpp, test_range_for_begin_end.cpp ✅

## Priority Investigation Areas

1. **Lambda capture** - 2 tests with lambda capture and decay (top priority)
2. **Floating-point register spilling** - 2 remaining tests (test_comprehensive_registers.cpp, test_register_spilling.cpp)
3. **Exception handling** - 2 tests requiring complete Linux exception support
4. **Template specialization** - 2 tests with member function specialization
5. **Variadic arguments** - 2 tests with va_list implementation issues
6. **Function pointer members** - 7 compiler crashes (pre-existing issue)

---

*Last Updated: 2025-12-20 (after XMM register handling fix)*
*Status: 635/661 tests passing (96.1%), 18 crashes, 1 timeout, 2 link failures, 7 compiler crashes*
*Run validation: `cd /home/runner/work/FlashCpp/FlashCpp && ./tests/validate_return_values.sh`*
