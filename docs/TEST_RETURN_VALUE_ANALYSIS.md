# Test Return Value Analysis

## Current Status (2025-12-20)

**631/661 tests passing (95.5%)**
- 22 runtime crashes (down from 26)
- 1 timeout (infinite loop)
- 1 link failure
- 7 compiler crashes (function pointer members - pre-existing issue)

**Run validation:** `cd /home/runner/work/FlashCpp/FlashCpp && ./tests/validate_return_values.sh`

## Key Note on Return Values

On Unix/Linux, `main()` return values are masked to 0-255 (8-bit). Values >255 are truncated via `value & 0xFF`.
- Returning 300 → exit code 44
- Returning 3000 → exit code 184
- **This is expected OS behavior, not a compiler bug**

## Recent Fixes (2025-12-20)

**Latest Fix: Pointer Variable Size in IR Generation**
- **Issue**: Pointer variables were incorrectly sized as their pointee type size (e.g., 32 bits for `int*`) instead of pointer size (64 bits)
- **Root Cause**: `calculateIdentifierSizeBits()` in CodeGen.h:4988 returned `type_node.size_in_bits()` for pointers, which is the pointee size
- **Fix**: Changed pointer and array identifiers to always return 64 bits (pointer size on x64)
  - Arrays decay to pointers when used as identifiers
  - Pointer variables must be represented as 64-bit values in IR
  - Element/pointee size is handled separately in pointer arithmetic operations
- **Status**: ✅ COMPLETE - Pointer arithmetic and range-based for loops now work correctly
- **Tests Fixed (4)**:
  - test_range_for.cpp - Range-based for loop over array
  - test_range_for_begin_end.cpp - Range-based for with begin/end ✓ returns exact expected value (150)
  - test_range_for_const_ref.cpp - Range-based for with const reference
  - test_custom_container.cpp - Custom container with iterators ✓ returns exact expected value (30)
- **Note**: test_pointer_arithmetic and test_pointer_loop still crash due to separate struct padding issues

**Previous Fix: Array Element Size for Struct Arrays**
- **Issue**: Array element addresses were calculated incorrectly for struct arrays, causing array indexing and pointer arithmetic to fail
- **Root Cause**: `generateIdentifierIr()` in CodeGen.h returned element size 0 for array identifiers instead of the actual element size
- **Fix**: Updated both DeclarationNode and VariableDeclarationNode branches to properly calculate element size from struct type info
  - Added check for array types to return element size (not total array size)
  - For struct elements, fetch size from gTypeInfo[type_index]->getStructInfo()->total_size
  - Also fixed `generateArraySubscriptIr()` to preserve correct element size
- **Status**: ✅ COMPLETE - Array indexing and pointer arithmetic now work correctly
- **Tests Fixed**: Simple array operations, pointer increment, basic for loops with pointer arithmetic
- **Note**: Discovered related issue with struct padding/alignment affecting some tests (see Known Issues)

**Previous Fix: Pointer Member Access Type Checking**
- **Issue**: Type checking rejected pointers to structs in member access expressions (e.g., `P* pp; pp->member`)
- **Root Cause**: `generateMemberAccessIr()` checked `is_struct_type()` which rejected pointers
- **Fix**: Modified type validation to accept: `is_struct_type(type) || (pointer_depth() > 0 && type_index() > 0)`
- **Status**: ✅ COMPLETE

<details>
<summary><strong>Completed Fixes (click to expand)</strong></summary>

- ✅ **Pointer variable size** (2025-12-20) - Fixed pointer identifiers to use 64-bit pointer size (4 tests)
- ✅ **Array element size** (2025-12-20) - Fixed struct array indexing and pointer arithmetic
- ✅ **Pointer member access type checking** (2025-12-20) - Allow pointers to structs in member access
- ✅ **Pure virtual functions** - Vtable entries use `__cxa_pure_virtual` for abstract classes
- ✅ **sizeof for struct arrays** - Fixed division by zero calculating sizeof(array_of_structs)
- ✅ **Stack alignment** (6 tests) - Fixed floating-point crashes with printf
- ✅ **Function pointers** (2 tests) - Global and pointer parameter function pointer calls  
- ✅ **Dereference register corruption** (1 test) - Clear stale register associations
- ✅ **Pointer member size** (7 tests) - Pointers/references in structs use correct 64-bit size
- ✅ **Temp variable stack allocation** (23 tests) - Fixed handleMemberAccess offset handling
- ✅ **Heap allocation constructor** - Fixed LEA vs MOV for heap vs stack objects
- ✅ **Multi-level pointer dereference** (2025-12-17) - Fixed type_index vs pointer_depth issue

</details>

## Known Issues

### Struct Padding/Alignment
FlashCpp does not correctly calculate struct padding for alignment. For example:
```cpp
struct P {
    int x;     // 4 bytes at offset 0
    int* p;    // 8 bytes - should be at offset 8, but FlashCpp puts it at offset 4
};
```
- **Expected**: sizeof(P) = 16 (with 4 bytes padding)
- **Actual**: FlashCpp calculates sizeof(P) = 12 (no padding)
- **Impact**: Affects tests using structs with mixed-size members (pointers, doubles, etc.)
- **Affected tests**: test_pointer_loop, test_pointer_arithmetic, and potentially others

### Compiler Crashes (7 files)
Function pointer member tests cause compiler hangs/crashes (pre-existing issue, not related to pointer size fix):
- test_funcptr_call_noinit.cpp, test_funcptr_minimal.cpp, test_func_ptr_simple.cpp
- test_funcptr_global.cpp, test_funcptr_param.cpp, test_funcptr_member_init.cpp
- test_func_ptr_struct_only.cpp, test_funcptr_noinit.cpp, test_member_init.cpp
- **Issue**: Compiler enters infinite loop or crashes when processing structs with function pointer members
- **Status**: Under investigation - not caused by recent changes

## Remaining Crashes (22 files + 1 timeout)

**Common crash signals:**
- Signal 11 (SIGSEGV) - Segmentation fault (most common)
- Signal 7 (SIGBUS) - Bus error
- Signal 4 (SIGILL) - Illegal instruction

### Crash Categories

1. **Floating-point register/stack** (5 files) - XMM register spilling, >8 float/double parameters
   - test_mixed_float_double_params.cpp, test_float_register_spilling.cpp, test_all_xmm_registers.cpp
   - test_comprehensive_registers.cpp, test_register_spilling.cpp (SIGBUS)

2. **Lambda** (2 files) - Lambda capture and decay to function pointers
   - test_lambda_decay.cpp, test_lambda_cpp20_comprehensive.cpp

3. **Exceptions** (2 files) - Incomplete Linux exception support
   - test_exceptions_basic.cpp, test_exceptions_nested.cpp

4. **Spaceship operator** (1 file) - C++20 three-way comparison (SIGILL)
   - spaceship_default.cpp

5. **RVO/NRVO** (1 file) - Return value optimization edge cases
   - test_rvo_very_large_struct.cpp

6. **Template specialization** (2 files)
   - test_spec_member_only.cpp, test_specialization_member_func.cpp

7. **Variadic arguments** (2 files) - va_list/va_arg implementation
   - test_va_implementation.cpp, test_varargs.cpp

8. **Struct padding** (2 files) - Incorrect struct size calculation due to missing padding
   - test_pointer_loop.cpp, test_pointer_arithmetic.cpp

9. **Other** (4 files)
    - test_abstract_class.cpp (vtable/typeinfo relocation issues)
    - test_stack_overflow.cpp
    - test_template_complex_substitution.cpp, test_ten_mixed.cpp

10. **Timeout** (1 file) - Infinite loop or hang
    - test_xvalue_all_casts.cpp

<details>
<summary><strong>Complete crash list (22 crashes + 1 timeout)</strong></summary>

1. spaceship_default.cpp (SIGILL)
2. test_abstract_class.cpp (SIGSEGV)
3. test_all_xmm_registers.cpp (SIGSEGV)
4. test_comprehensive_registers.cpp (SIGSEGV)
5. test_exceptions_basic.cpp (SIGSEGV)
6. test_exceptions_nested.cpp (SIGSEGV)
7. test_float_register_spilling.cpp (SIGSEGV)
8. test_lambda_cpp20_comprehensive.cpp (SIGSEGV)
9. test_lambda_decay.cpp (SIGSEGV)
10. test_mixed_float_double_params.cpp (SIGSEGV)
11. test_pointer_arithmetic.cpp (SIGSEGV)
12. test_pointer_loop.cpp (SIGSEGV)
13. test_register_spilling.cpp (SIGSEGV)
14. test_rvo_very_large_struct.cpp (SIGSEGV)
15. test_spec_member_only.cpp (SIGSEGV)
16. test_specialization_member_func.cpp (SIGSEGV)
17. test_stack_overflow.cpp (SIGSEGV)
18. test_template_complex_substitution.cpp (SIGSEGV)
19. test_ten_mixed.cpp (SIGSEGV)
20. test_va_implementation.cpp (SIGSEGV)
21. test_varargs.cpp (SIGSEGV)
22. test_xvalue_all_casts.cpp (timeout)

**Fixed in this update (moved from crash list)**:
- test_range_for.cpp - Now passing (returns 119)
- test_range_for_begin_end.cpp - Now passing (returns 150) ✓
- test_range_for_const_ref.cpp - Now passing (returns 86)
- test_custom_container.cpp - Now passing (returns 30) ✓

</details>

## Priority Investigation Areas

1. **Struct padding/alignment** - Affects test_pointer_loop.cpp and test_pointer_arithmetic.cpp - struct sizes calculated without proper padding
2. **Floating-point register spilling** - 5 tests with >8 float/double parameters or heavy XMM register usage
3. **Lambda capture** - 2 tests with lambda capture and decay to function pointers
4. **Exception handling** - 2 tests requiring complete Linux exception support
5. **Abstract classes** - test_abstract_class.cpp has vtable/typeinfo relocation issues
6. **Spaceship operator** - 1 test generating illegal CPU instruction (SIGILL)
7. **Function pointer members** - 7 compiler crashes with function pointer struct members (pre-existing issue)

---

*Last Updated: 2025-12-20 (after pointer variable size fix)*
*Status: 631/661 tests passing (95.5%), 22 crashes, 1 timeout, 1 link failure, 7 compiler crashes*
*Run validation: `cd /home/runner/work/FlashCpp/FlashCpp && ./tests/validate_return_values.sh`*
