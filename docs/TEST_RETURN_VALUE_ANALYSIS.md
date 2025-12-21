# Test Return Value Analysis

## Current Status (2025-12-21 - After Nested Member Access Fix)

**654/669 tests passing (97.8%)**
- 11 runtime crashes (C++ runtime compatibility issues)
- 1 compilation hang (test_lambda_copy_this_mutation.cpp - under investigation)
- 0 link failures ✅

**Run validation:** `cd /home/runner/work/FlashCpp/FlashCpp && ./tests/validate_return_values.sh`

**Latest Progress:**
- ✅ Fixed nested member access with AddressOf (Session 12)
  - `arr[i].member1.member2` assignments now work correctly
  - Fixed `test_addressof_multilevel.cpp` ✅
  - Nested member accesses now unwrap lvalue metadata and combine offsets
- 📊 Current: 654/669 passing (97.8%), 11 crashes, 1 hang

## Key Note on Return Values

On Unix/Linux, `main()` return values are masked to 0-255 (8-bit). Values >255 are truncated via `value & 0xFF`.
- Returning 300 → exit code 44
- Returning 3000 → exit code 184
- **This is expected OS behavior, not a compiler bug**

## Recent Fixes (Sessions 11-12)

### Session 12 (2025-12-21): Nested Member Access with AddressOf
**Fixed nested member access for assignments and address-of operations**
- **Issue**: `arr[1].inner.y = 70` didn't work
  - Code loaded `arr[1].inner` (8-byte struct) into temp location
  - Tried to modify `.y` within that temp copy
  - Never wrote modified struct back to original location
  - Result: assignment had no effect
- **Solution**:
  - Detect TempVar base_object with Member lvalue metadata
  - Unwrap nested member accesses to get ultimate base object
  - Combine offsets (e.g., arr[1] offset + inner offset + y offset)
  - Emit single MemberAccess/MemberStore with combined offset
  - Pass context parameter through recursive calls to avoid loading intermediate structs
- **Files Modified**: `src/CodeGen.h` (lines 10477, 10808-10856)
- **Impact**: Fixed `test_addressof_multilevel.cpp` - 654/669 passing (up from 653)

### Session 11 (2025-12-21): Pointer Array Element Size
**Fixed array subscript for arrays of pointers** - See expanded section below for details.
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
- **Impact**: Fixed `test_addressof_multilevel.cpp` - 654/669 passing (up from 653)

### Session 11 (2025-12-21): Pointer Array Element Size
**Fixed array subscript for arrays of pointers**
- **Issue**: Arrays of pointers (e.g., `int* ptrs[3]`) treated elements as 32-bit instead of 64-bit
- **Solution**: Check `pointer_depth() > 0` to use 64-bit size for pointer array elements, track pointer depth through operations
- **Files Modified**: `src/CodeGen.h` (lines 5839-6115, 10228-10361)
- **Impact**: Fixed `test_pointer_arithmetic.cpp`

<details>
<summary><strong>Earlier Fixes (Sessions 1-10) - Click to expand</strong></summary>

### Sessions 6-10 (2025-12-21): Code Generation Fixes
- **Session 10**: TempVar offset calculation - added `size_in_bits` parameters
- **Session 9b**: Array store stack alignment - removed PUSH/POP causing corruption
- **Session 9a**: Array element size calculation - use `type_node.size_in_bits()`
- **Session 8**: Large struct stack allocation - fixed temp vars for structs >8 bytes
- **Session 7**: Template specialization return types - fixed 3 tests
- **Session 6**: Virtual function vtable pointer dereference - fixed 2 tests

### Sessions 2-5 (2025-12-21): Core Functionality
- Array constructor calls, typeinfo generation (eliminated link failures), rvalue references, struct alignment

### Earlier Sessions (2025-12-20/21): Foundation
- Lambda decay, float literals, range-for, pointer sizes, vtable generation, pure virtual functions, stack alignment, function pointers, array handling, heap allocation

</details>

## Known Issues & Limitations

### Float-to-Int Conversion
Tests may return incorrect results but don't crash. Low priority.

### ~~Nested Member Access with AddressOf~~ ✅ FIXED
~~`&arr[i].member` works, but `&arr[i].member1.member2` doesn't.~~
**Fixed in Session 12** - Nested member access now works correctly.

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

*Last Updated: 2025-12-21 (Nested member access fix - Session 12)*
*Status: 654/669 tests passing (97.8%), 11 crashes, 1 compilation hang*
*Run validation: `cd /home/runner/work/FlashCpp/FlashCpp && ./tests/validate_return_values.sh`*
