# Test Return Value Analysis

## Current Status (2025-12-20 - Updated Again)

**639/661 tests passing (96.7%)**
- 13 runtime crashes (down from 21)
- 1 timeout (infinite loop)
- 2 link failures

**Run validation:** `cd /home/runner/work/FlashCpp/FlashCpp && ./tests/validate_return_values.sh`

## Key Note on Return Values

On Unix/Linux, `main()` return values are masked to 0-255 (8-bit). Values >255 are truncated via `value & 0xFF`.
- Returning 300 → exit code 44
- Returning 3000 → exit code 184
- **This is expected OS behavior, not a compiler bug**

## Recent Fixes (2025-12-20)

**Latest Fix: Float Literal Initialization & OpCodeWithSize Buffer Overflow**
- **Issue**: Float/double variable initialization crashed with segfault when using variables with stack offsets requiring 32-bit displacements (offset < -128 or > 127)
- **Root Cause #1**: Float literals were loaded into GPRs and stored using integer mov instructions, not properly initializing them as float values
- **Root Cause #2**: `OpCodeWithSize` buffer was only 8 bytes, but SSE float mov instructions can be 9 bytes (Prefix + REX + 2-byte opcode + ModR/M + 4-byte displacement), causing buffer overflow that corrupted instruction encoding
- **Fix**: 
  - Modified `handleVariableDecl` in IRConverter.h to store float literals directly to memory using `emitMovDwordPtrImmToRegOffset` for 32-bit floats (more efficient)
  - For doubles, load into GPR and store with correct size
  - Increased `MAX_MOV_INSTRUCTION_SIZE` from 8 to 9 bytes to accommodate SSE instructions
- **Status**: ✅ COMPLETE
- **Tests Fixed (8)**: All floating-point register spilling tests now pass without crashing
  - test_float_register_spilling.cpp ✓ (no crash)
  - test_mixed_float_double_params.cpp ✓ (no crash)
  - test_all_xmm_registers.cpp ✓ (no crash)
  - test_comprehensive_registers.cpp ✓ (no crash)
  - test_register_spilling.cpp ✓ (no crash)
  - Plus 3 other floating-point tests
- **Known Issue**: Float-to-int conversions in assignments don't generate FloatToInt IR, causing incorrect return values (pre-existing bug, not introduced by this fix)

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

## Remaining Crashes (13 files + 1 timeout)

**Current: 13 crashes, 1 timeout** (down from 21 - floating-point tests now fixed!)

### Crash Categories

1. **Lambda** (2 files) - Capture and decay to function pointers  
   test_lambda_decay.cpp, test_lambda_cpp20_comprehensive.cpp

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

**Fixed**: All 5 floating-point register spilling tests (test_float_register_spilling.cpp, test_mixed_float_double_params.cpp, test_all_xmm_registers.cpp, test_comprehensive_registers.cpp, test_register_spilling.cpp) plus 3 additional floating-point tests now pass without crashing!

## Priority Investigation Areas

1. **Lambda capture** - 2 tests with lambda capture and decay (top priority - manageable scope)
2. **Template specialization** - 2 tests with member function specialization
3. **Exception handling** - 2 tests requiring complete Linux exception support
4. **Variadic arguments** - 2 tests with va_list implementation issues
5. **Three-way comparison** - 1 test with C++20 spaceship operator

---

*Last Updated: 2025-12-20 (after floating-point initialization fix)*
*Status: 639/661 tests passing (96.7%), 13 crashes, 1 timeout, 2 link failures*
*Run validation: `cd /home/runner/work/FlashCpp/FlashCpp && ./tests/validate_return_values.sh`*
