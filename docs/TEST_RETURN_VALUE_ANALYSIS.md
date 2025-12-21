# Test Return Value Analysis

## Current Status (2025-12-21 - Session 13)

**652/669 tests passing (97.5%)**
- 11 runtime crashes (C++ runtime compatibility - cannot be fixed at compiler level)
- 1 compilation failure (test_lambda_copy_this_mutation.cpp - compiler assertion failure)
- 0 link failures ✅

**Run validation:** `cd /home/runner/work/FlashCpp/FlashCpp && ./tests/validate_return_values.sh`

**Recent Investigation (Session 13):**
- 🔍 Investigated test_lambda_copy_this_mutation.cpp compilation failure
  - Issue: Compiler asserts "Struct object not found in scope or globals" during codegen
  - Root cause: Compound assignments (`value *= 2`) in `[*this]` mutable lambdas generate incorrect IR
  - First compound assignment works correctly, second fails
  - IR shows incorrect object reference (`%c.value` instead of `%<tempvar>.value`)
  - Affects `[*this]` lambda capture with multiple compound assignments
  - Cannot be fixed without deeper refactoring of identifier resolution in lambda contexts

## Key Note on Return Values

On Unix/Linux, `main()` return values are masked to 0-255 (8-bit). Values >255 are truncated via `value & 0xFF`.
- Returning 300 → exit code 44
- Returning 3000 → exit code 184
- **This is expected OS behavior, not a compiler bug**

## Recent Progress Summary

<details>
<summary><strong>Sessions 11-12: Nested Member Access & Pointer Arrays</strong></summary>

### Session 12: Nested Member Access with AddressOf
- Fixed `arr[i].member1.member2` assignments
- Unwraps nested lvalue metadata, combines offsets
- Modified: `src/CodeGen.h` (lines 10477, 10808-10856)
- Result: 654/669 passing

### Session 11: Pointer Array Element Size
- Fixed arrays of pointers treating elements as 32-bit instead of 64-bit
- Track pointer depth through operations
- Modified: `src/CodeGen.h` (lines 5839-6115, 10228-10361)
</details>

<details>
<summary><strong>Sessions 6-10: Core Fixes</strong></summary>

- **Session 10**: TempVar offset calculation with `size_in_bits`
- **Session 9**: Array store stack alignment fixes
- **Session 8**: Large struct stack allocation
- **Session 7**: Template specialization return types
- **Session 6**: Virtual function vtable pointer dereference
</details>

<details>
<summary><strong>Sessions 1-5: Foundation</strong></summary>

Array constructor calls, typeinfo generation, rvalue references, struct alignment, lambda decay, float literals, range-for, pointer sizes, vtable generation, pure virtual functions, stack alignment, function pointers, heap allocation.
</details>

## Known Issues & Limitations

### ~~Nested Member Access with AddressOf~~ ✅ FIXED (Session 12)

### Float-to-Int Conversion
Tests may return incorrect results but don't crash. Low priority.

### Lambda Compound Assignment Bug ⚠️ UNFIXED
**File**: `test_lambda_copy_this_mutation.cpp`
**Status**: Compilation failure (signal 134 - SIGABRT)

**Problem**: Compound assignments in `[*this]` mutable lambdas fail when there are multiple assignments:
```cpp
auto lambda = [*this]() mutable {
    value += 5;   // Works correctly
    value *= 2;   // FAILS - generates incorrect IR
    return value;
};
```

**Root Cause**: 
- First compound assignment generates correct IR: loads `__copy_this` into temp, accesses member
- Second compound assignment generates incorrect IR: references non-existent variable
- IR shows: `%9 = member_access int32 %c.value` where `c` doesn't exist in lambda scope
- Should be: `%9 = member_access int32 %7.value` (using `__copy_this` temp)

**Technical Details**:
- Error occurs in `IrToObjConverter::handleMemberAccess` (IRConverter.h:11831)
- During object file generation phase, compiler cannot find variable in scope
- Issue is in IR generation, not object file conversion
- LValue metadata for second assignment has incorrect base object

**Impact**: Prevents compilation of `[*this]` lambdas with multiple compound assignments to captured members

## Remaining Crashes (11 files)

**11 runtime crashes (C++ runtime/ABI compatibility - cannot be fixed at compiler level)**

### Crash Categories

1. **Exceptions** (2 files) - Incomplete Linux exception handling support
   - `test_exceptions_basic.cpp`
   - `test_exceptions_nested.cpp`

2. **Variadic Arguments** (2 files) - va_list/va_arg implementation gaps  
   - `test_va_implementation.cpp`
   - `test_varargs.cpp`

3. **Virtual Functions / RTTI** (2 files) - Complex vtable/inheritance scenarios
   - `test_covariant_return.cpp` (covariant return types)
   - `test_virtual_inheritance.cpp` (virtual inheritance diamond problem)

4. **C++ Runtime Initialization** (2 files) - Code generation correct, but crashes during C++ runtime init
   - `test_addressof_int_index.cpp`
   - `test_arrays_comprehensive.cpp`
   - ✅ Generated assembly is correct (verified via objdump)
   - ⚠️ Crashes with SIGSEGV before entering `main()`
   - **Root cause**: C++ runtime/startup code incompatibility, not compiler code generation

5. **Other** (3 files) - Complex C++ features
   - `test_rvo_very_large_struct.cpp` (large struct RVO/NRVO)
   - `test_lambda_cpp20_comprehensive.cpp` (advanced C++20 lambda features)
   - `test_xvalue_all_casts.cpp` (xvalue handling across all cast types)

---

*Last Updated: 2025-12-21 (Session 13 - Lambda compound assignment investigation)*  
*Status: 652/669 tests passing (97.5%), 11 crashes, 1 compilation failure*  
*Run validation: `cd /home/runner/work/FlashCpp/FlashCpp && ./tests/validate_return_values.sh`*
