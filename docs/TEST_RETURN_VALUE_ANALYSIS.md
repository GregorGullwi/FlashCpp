# Test Return Value Analysis

# Test Return Value Analysis

## Current Status (2025-12-21 - After Stack Allocation Investigation)

**655/669 tests passing (97.9%)**
- 9 runtime crashes (unchanged)
- 0 link failures ✅

**Run validation:** `cd /home/runner/work/FlashCpp/FlashCpp && ./tests/validate_return_values.sh`

**Latest Progress:**
- 🔧 Investigated large struct return value issues (test_rvo_very_large_struct.cpp)
- ✅ Fixed temp variable stack allocation for large structs
  - Modified getStackOffsetFromTempVar to accept size_in_bits parameter
  - Updated scope_stack_space calculation to account for full struct size
  - Function call results now allocated with correct size (not just 8 bytes)
  - Constructor temp vars now allocated with struct size
- 🐛 Identified separate bug: constructor array writes not generated (affects test_rvo_very_large_struct.cpp)
- 📊 Current: 655/669 passing (97.9%), same as before (constructor bug blocks improvement)

## Key Note on Return Values

On Unix/Linux, `main()` return values are masked to 0-255 (8-bit). Values >255 are truncated via `value & 0xFF`.
- Returning 300 → exit code 44
- Returning 3000 → exit code 184
- **This is expected OS behavior, not a compiler bug**

## Completed Fixes Summary

### Latest Investigation (2025-12-21 Session 8)
**Large Struct Stack Allocation** - Fixed temp variable allocation for large structs
- **Issue**: Large structs (>8 bytes) returned from functions were only allocated 8 bytes of stack space
  - TempVars were always allocated with fixed 8-byte size regardless of actual type size
  - Caused stack corruption when copying large struct returns
  - Example: 80-byte struct only got 48 bytes allocated, reading/writing beyond allocated space
- **Root Cause**: `getStackOffsetFromTempVar` always incremented by 8 bytes per temp var
  - Function call results: `allocateStackSlotForTempVar` didn't pass size information
  - Constructor calls: Size wasn't looked up from struct type information
  - Stack prologue size computed correctly, but individual allocations were wrong
- **Solution**:
  1. Added `size_in_bits` parameter to `getStackOffsetFromTempVar` (default 64 for compatibility)
  2. Calculate `size_in_bytes` with proper rounding and 8-byte alignment
  3. Update `next_temp_var_offset_` by `size_in_bytes` instead of fixed 8
  4. Fix `scope_stack_space` calculation to account for full struct size (end_offset = offset - size + 8)
  5. Pass `return_size_in_bits` from `CallOp` to allocation in `handleFunctionCall`
  6. Look up struct size from type system in `handleConstructorCall` and pass to allocation
- **Files Modified**: `src/IRConverter.h` (lines 4714-4775, 5707, 6144-6163)
- **Impact**:
  - ✅ Stack allocation now correct: test case went from 0x30 (48) to 0x70 (112) bytes
  - ✅ Prevents stack corruption for large struct returns
  - ⚠️ test_rvo_very_large_struct.cpp still crashes due to separate constructor array write bug
  - ⚠️ No test count improvement yet (655/669) - blocked by constructor bug

**Known Issue**: Constructor array element assignment not generating store instructions. In `LargeStruct(int start)` constructor with `for(int i=0; i<20; i++) values[i] = start+i;`, the loop computes values but never writes them to memory. This affects all tests with struct constructors that initialize arrays.

<details>
<summary><strong>Previous Fixes (Click to expand)</strong></summary>

### Session 7 Fix (2025-12-21)
**Template Specialization Member Function Return Types** - Fixed incorrect return type substitution
- **Issue**: Member functions in template partial specializations didn't substitute template parameters
  - For `Container<T*>::get()` returning `T*`, the return type wasn't being substituted when instantiated
  - When instantiated as `Container<int*>`, `get()` should return `int*` but was returning `T*`
  - This caused pointer return values to be treated as 32-bit (Type::UserDefined) instead of 64-bit
  - Led to segmentation faults when dereferencing returned pointers
- **Root Cause**: Two bugs in template instantiation and code generation
  1. Parser: Member functions from partial specializations copied pattern's Type::UserDefined without substitution
  2. CodeGen: Member function calls used temporary func_decl_node instead of actual member function for return type
- **Solution**:
  1. Parser.cpp (lines 21866-21919): Substitute template parameters in member function return types during instantiation
  2. CodeGen.h (lines 9835-9848): Use actual member function declaration from struct_info for return type information
- **Files Modified**: `src/Parser.cpp`, `src/CodeGen.h`
- **Impact**:
  - ✅ Fixed test_spec_member_only.cpp: returns 0 (expected) - partial specialization member functions work
  - ✅ Fixed test_specialization_member_func.cpp: returns 0 (expected) - full+partial spec member functions work
  - ✅ Fixed test_template_complex_substitution.cpp: returns 0 (expected) - complex template member types work
  - ✅ Test count improved: 652/669 → 655/669 (97.9%)
  - ✅ Crashes reduced: 12 → 9

### Session 6 Fix (2025-12-21)
**Virtual Function Call Vtable Dereferencing** - Fixed incorrect virtual call code generation
- **Issue**: Virtual calls were missing the vtable pointer dereference step
  - Code was loading object address and then indexing directly: [object + vtable_index*8]
  - Should load vtable pointer first: [object + 0], then index: [vtable + vtable_index*8]
  - Also failed to distinguish between pointer objects (64-bit) vs direct objects (>64-bit)
- **Root Cause**: `handleVirtualCall` in IRConverter.h had incomplete vtable lookup sequence
- **Solution**:
  - Added missing vtable pointer load step before indexing into vtable
  - Use platform-specific register for 'this' (RDI on Linux, RCX on Windows)
  - Distinguish pointer objects (MOV to load value) from direct objects (LEA to get address)
- **Files Modified**: `src/IRConverter.h` (lines 6520-6610)
- **Impact**:
  - ✅ Fixed test_abstract_class.cpp: returns 98 (expected) - pure virtual functions work
  - ✅ Fixed test_virtual_basic.cpp: returns 125 (expected) - direct object virtual calls work
  - ✅ Test count improved: 651/669 → 652/669 (97.5%)
  - ✅ Crashes reduced: 13 → 12

### Session 5 Fix (2025-12-21)
**Array Constructor Calls for Structs** - Fixed incomplete struct array initialization
- Issue: `S arr[3]{}` only called constructor once instead of per element
- Solution: Generate loop with `ConstructorCall` IR instruction per array element
- Files: `src/CodeGen.h`, `src/IRConverter.h`, `src/IRTypes.h`
- Impact: Fixed test_struct_default_init_addressof.cpp

### Session 4 Fix (2025-12-21)
**Typeinfo Generation** - Fixed missing typeinfo symbols for polymorphic classes
- Issue: Classes without base classes never got RTTI; vector invalidation
- Solution: Added `buildRTTI()` to `finalize()`; switched to `std::deque` for storage
- Files: `src/AstNodeTypes.h`, `src/AstNodeTypes.cpp`
- Impact: Eliminated all link failures (2 → 0)

### Session 3 Fix (2025-12-21)
**Rvalue Reference Handling** - Fixed regression in reference dereferencing
- Only dereference lvalue references, not rvalue references
- Files: `src/CodeGen.h`

### Session 2 Fixes (2025-12-21)
**Struct Member Alignment, Reference Dereferencing, Return Type Sizes**
- Fixed template member alignment, lvalue reference dereferencing, reference return sizes
- Files: `src/Parser.cpp`, `src/CodeGen.h`

### Earlier Fixes (2025-12-20/21)
- ✅ Lambda decay, float literals, range-for loops, AddressOf member access
- ✅ Pointer variable sizes, vtable generation, pure virtual functions
- ✅ Stack alignment, function pointers, array handling
- ✅ Heap allocation, multi-level pointers, temp variable allocation

</details>

## Known Issues & Limitations

<details>
<summary><strong>Resolved Issues - Click to expand</strong></summary>

### Template Specialization Member Functions - RESOLVED ✅
test_spec_member_only.cpp, test_specialization_member_func.cpp, and test_template_complex_substitution.cpp now pass after fixing template parameter substitution in member function return types.

### Struct Padding/Alignment - RESOLVED ✅
Investigation revealed struct padding IS working correctly. Crashes were caused by array element size bugs (now fixed).

### Spaceship Operator - RESOLVED ✅
spaceship_default.cpp now passes after fixing reference return type handling.

### Default-Initialized Struct Array - RESOLVED ✅
test_struct_default_init_addressof.cpp fixed by implementing per-element constructor calls for arrays.

### Virtual Function Calls - PARTIALLY RESOLVED ✅
Fixed vtable dereferencing for basic virtual calls. test_abstract_class.cpp and test_virtual_basic.cpp now work. Covariant returns and virtual inheritance still have issues.

</details>

### Float-to-Int Conversion in Assignments
**Status**: Known issue, low priority. Tests may return incorrect float-to-int results but don't crash.

### Nested Member Access with AddressOf
**Status**: Known limitation. `&arr[i].member` works, but `&arr[i].member1.member2` doesn't.

## Remaining Crashes (9 files)

**Current: 9 crashes (down from 12), 0 link failures**

### Crash Categories

1. **Exceptions** (2 files) - Incomplete Linux exception support
   - test_exceptions_basic.cpp, test_exceptions_nested.cpp

2. **Variadic arguments** (2 files)
   - test_va_implementation.cpp, test_varargs.cpp

3. **Virtual function / RTTI issues** (2 files) - Complex vtable scenarios
   - test_covariant_return.cpp (covariant return types)
   - test_virtual_inheritance.cpp (virtual inheritance)

4. **Other issues** (3 files)
   - test_rvo_very_large_struct.cpp (large struct RVO)
   - test_lambda_cpp20_comprehensive.cpp (complex C++20 lambda features)
   - test_xvalue_all_casts.cpp (cast handling)

## Priority Investigation Areas

1. **Covariant returns & virtual inheritance** - 2 tests with complex vtable layouts
2. **Exception handling** - 2 tests requiring complete Linux exception support
3. **Variadic arguments** - 2 tests with va_list implementation gaps
4. **Lambda capture** - 1 test with complex C++20 lambda features
5. **Large struct RVO** - 1 test with very large struct return value optimization
6. **Cast handling** - 1 test with xvalue and all cast types

---

*Last Updated: 2025-12-21 (template specialization member function fix)*
*Status: 655/669 tests passing (97.9%), 9 crashes, 0 link failures*
*Run validation: `cd /home/runner/work/FlashCpp/FlashCpp && ./tests/validate_return_values.sh`*
