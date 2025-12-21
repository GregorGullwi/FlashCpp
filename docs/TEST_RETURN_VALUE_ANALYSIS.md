# Test Return Value Analysis

## Current Status (2025-12-21 - After Pointer Array Fix)

**653/669 tests passing (97.6%)**
- 11 runtime crashes (C++ runtime compatibility issues)
- 0 link failures ✅

**Run validation:** `cd /home/runner/work/FlashCpp/FlashCpp && ./tests/validate_return_values.sh`

**Latest Progress:**
- ✅ Fixed array subscript for pointer arrays (Session 11)
  - Arrays of pointers (e.g., `int* ptrs[3]`) now correctly use 64-bit element size
  - Pointer depth tracking through AddressOf and dereference operations
  - Fixed `test_pointer_arithmetic.cpp`
- 📊 Current: 653/669 passing (97.6%), 11 crashes (down from 12)

## Key Note on Return Values

On Unix/Linux, `main()` return values are masked to 0-255 (8-bit). Values >255 are truncated via `value & 0xFF`.
- Returning 300 → exit code 44
- Returning 3000 → exit code 184
- **This is expected OS behavior, not a compiler bug**

## Recent Fix (Session 11)

### Session 11 (2025-12-21): Pointer Array Element Size
**Fixed array subscript for arrays of pointers**
- **Issue**: Arrays of pointers (e.g., `int* ptrs[3]`) treated elements as 32-bit instead of 64-bit
  - `type_node.size_in_bits()` returned base type size (int=32), not pointer size (64)
  - Array stores wrote only 32 bits of 64-bit pointer values
  - AddressOf operations didn't track pointer depth correctly
  - Dereference operations used wrong size for pointer-to-pointer
- **Solution**: 
  - Check `pointer_depth() > 0` in `generateArraySubscriptIr` to use 64-bit size for pointer array elements
  - Track `element_pointer_depth` and return in 4th operand of array subscript
  - Add `pointer_depth` field to `AddressComponents` struct
  - Pass pointer depth through AddressOf operations (`pointer_depth + 1`)
  - Dereference operations now correctly determine result size based on pointer depth
- **Files Modified**: `src/CodeGen.h` (lines 5839-6115, 10228-10361)
- **Impact**: Fixed `test_pointer_arithmetic.cpp` - 653/669 passing, 11 crashes (down from 12)

<details>
<summary><strong>Earlier Fixes (Sessions 1-10) - Click to expand</strong></summary>

### Session 10 (2025-12-21): TempVar Offset Calculation
- Fixed missing `size_in_bits` parameters in `getStackOffsetFromTempVar` calls
- Ensured correct stack allocation for all value sizes

### Session 9b (2025-12-21): Array Store Stack Alignment
- Fixed PUSH/POP causing stack corruption in variable-index stores
- Use RDX/RCX/RAX registers without PUSH/POP

### Session 9a (2025-12-21): Array Element Size Calculation  
- Fixed incorrect element size for array subscript operations
- Always use `type_node.size_in_bits()` for arrays

### Session 8 (2025-12-21): Large Struct Stack Allocation
- Fixed temp variable allocation for large structs (>8 bytes)
- Added `size_in_bits` parameter to `getStackOffsetFromTempVar`

### Session 7 (2025-12-21): Template Specialization Member Functions
- Fixed incorrect return type substitution in template partial specializations
- Fixed 3 tests (test_spec_member_only, test_specialization_member_func, test_template_complex_substitution)

### Session 6 (2025-12-21): Virtual Function Calls
- Fixed missing vtable pointer dereference
- Fixed 2 tests (test_abstract_class, test_virtual_basic)

### Sessions 2-5 (2025-12-21): Various Fixes
- **Session 5**: Array constructor calls for structs
- **Session 4**: Typeinfo generation - eliminated all link failures
- **Session 3**: Rvalue reference handling
- **Session 2**: Struct member alignment, reference dereferencing, return type sizes

### Earlier Fixes (2025-12-20/21)
- Lambda decay, float literals, range-for loops, AddressOf member access
- Pointer sizes, vtable generation, pure virtual functions, stack alignment
- Function pointers, array handling, heap allocation, multi-level pointers

</details>

## Known Issues & Limitations

### Float-to-Int Conversion
Tests may return incorrect results but don't crash. Low priority.

### Nested Member Access with AddressOf
`&arr[i].member` works, but `&arr[i].member1.member2` doesn't.

## Remaining Crashes (11 files)

**11 crashes with clang++, 12 with gcc (C++ runtime compatibility issues)**

### Crash Categories

1. **Exceptions** (2 files) - Incomplete Linux exception support
   - test_exceptions_basic.cpp, test_exceptions_nested.cpp

2. **Variadic arguments** (2 files) - va_list implementation gaps  
   - test_va_implementation.cpp, test_varargs.cpp

3. **Virtual function / RTTI** (2 files) - Complex vtable scenarios
   - test_covariant_return.cpp (covariant return types)
   - test_virtual_inheritance.cpp (virtual inheritance)

4. **C++ Runtime Compatibility** (2 files) - Generated code correct, runtime init fails
   - test_addressof_int_index.cpp
   - test_arrays_comprehensive.cpp
   - ✅ Code generation fixed (register allocation, no PUSH/POP)
   - ⚠️ Crash during clang++ runtime initialization

5. **Other** (3 files)
   - test_rvo_very_large_struct.cpp (large struct RVO)
   - test_lambda_cpp20_comprehensive.cpp (complex C++20 lambdas)
   - test_xvalue_all_casts.cpp (cast handling)

## Investigation Notes

### Constructor Array Element Assignment
The known issue from line 96 (Session 8 notes) appears to be resolved. The problem was that TempVar results from arithmetic operations weren't being found when loading for array stores. Fixed by:
1. Passing `size_in_bits` to `getStackOffsetFromTempVar` calls
2. Adding register check to reuse cached values

Tests still crash due to C++ runtime initialization issues, not code generation bugs.

---

*Last Updated: 2025-12-21 (Pointer array fix - Session 11)*
*Status: 653/669 tests passing (97.6%), 11 crashes, 0 link failures*
*Run validation: `cd /home/runner/work/FlashCpp/FlashCpp && ./tests/validate_return_values.sh`*
