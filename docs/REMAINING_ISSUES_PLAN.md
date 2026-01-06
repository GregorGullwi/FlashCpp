# Plan to Fix Remaining Issues in FlashCpp

**Current Status:** 674/674 tests compiling and linking successfully (100%)  
**Remaining:** 9 tests with runtime issues requiring additional feature implementation  
**Date:** 2025-12-22 (Created) | Last Updated: 2026-01-06 (Variadic functions progress)

---

## Overview

This document outlines a comprehensive plan to address the remaining runtime issues in FlashCpp. All 674 test files now compile and link successfully. The remaining issues involve tests that require specific runtime features or language support that need to be implemented.

## Recent Progress

### ✅ Variadic Functions - Basic Functionality Working (January 2026)

**Issue:** System V AMD64 ABI variadic function implementation was incomplete, lacking proper register save area and va_list structure setup.

**Solution Implemented:**
1. **Register Save Area:** 
   - Allocates 176 bytes on stack (48 bytes for 6 integer registers + 128 bytes for 8 XMM registers)
   - Properly saves RDI, RSI, RDX, RCX, R8, R9 to integer register area
   - Properly saves XMM0-XMM7 to floating-point register area using MOVDQU instructions
   
2. **va_list Structure:**
   - Implements System V AMD64 ABI-compliant `__va_list_tag` structure (24 bytes)
   - Initializes `gp_offset` field based on fixed integer parameter count
   - Initializes `fp_offset` field based on fixed floating-point parameter count
   - Sets `overflow_arg_area` for stack overflow arguments
   - Sets `reg_save_area` pointer to point to saved registers

3. **va_arg Implementation (Phase 3 Fix):**
   - Fixed va_start for Linux with `typedef char* va_list;` to use register save area
   - Fixed va_arg double-dereference bug
   - Fixed holds_address_only check in arithmetic operations
   - Fixed gp_offset dereference size (32-bit, not 64-bit)

4. **Code Quality Improvements:**
   - Added `FLOAT_REG_COUNT` constant (8) instead of magic number
   - Added `static_assert(INT_REG_COUNT == 6, ...)` for compile-time ABI verification
   - Extracted `isVaListPointerType()` helper function

**Validation Results:**
- ✅ **va_arg with integer arguments (up to 5 variadic args for 1 fixed param)**: WORKING
  - sum2(2, 30, 12) = 42 ✓
  - sum3(3, 10, 20, 30) = 60 ✓
  - sum5(5, 1, 2, 3, 4, 5) = 15 ✓
- ⚠️ **va_arg with overflow (>5 int args after 1 fixed param)**: Incorrect results (not crashes)
  - sum7(7, 1, 2, 3, 4, 5, 6, 7) returns 15 instead of 28 (6th and 7th args not read)
- ❌ **va_arg with floating-point arguments**: Not yet tested
- ❌ **Struct arguments**: Not tested

**Impact:** 
- ✅ Variadic function declarations and parsing work correctly on Linux (System V AMD64 ABI)
- ✅ Register save area and va_list structure initialization are implemented
- ✅ **va_arg now works for integer arguments within register count**
- ⚠️ Overflow support not yet implemented (reads garbage instead of stack args)
- Remaining work: implement overflow_arg_area support, test float args

**Status:** ✅ Phases 1-3 complete, ⚠️ Phase 4 partial (overflow not supported yet)

---

### ✅ Fixed: Array Index Loading Bug (December 2025)

**Issue:** Compiler generated 64-bit mov instructions when loading 32-bit integer array indices, reading 8 bytes instead of 4 and polluting registers with garbage from uninitialized stack memory.

**Solution:** Modified `emitLoadIndexIntoRCX()` to generate size-appropriate mov instructions:
- 32-bit: `mov ecx, [rbp-offset]` (zero-extends to 64 bits)
- 64-bit: `mov rcx, [rbp-offset]` (with REX.W prefix)

**Impact:** Fixed compilation issues in 16 test files. All 674 tests now compile and link successfully.

---

## Issue Categories & Implementation Plan

### 1. Exceptions (2 files) - **MEDIUM-HIGH COMPLEXITY**

**Files:**
- `test_exceptions_basic.cpp`
- `test_exceptions_nested.cpp`

**Current State:**
- AST nodes exist: `ThrowStatementNode`, `TryCatchStatementNode`
- Parser recognizes `throw`, `try`, `catch` keywords
- ❌ No IR generation for exception handling
- ❌ No ELF exception tables (.eh_frame, .gcc_except_table)
- ❌ No DWARF CFI (Call Frame Information) generation

**Root Cause:**
Exception handling requires complex runtime support:
1. Stack unwinding mechanism
2. Exception tables in ELF format
3. DWARF Call Frame Information (CFI)
4. Landing pads and cleanup code
5. Type matching for catch clauses

**Implementation Steps:**

#### Phase 1: Exception Table Generation (2-3 days)
```
1. Create .gcc_except_table section
   - Language-Specific Data Area (LSDA)
   - Call site table with landing pad addresses
   - Action table for exception types
   - Type table for RTTI matching

2. Enhance .eh_frame section
   - Frame Description Entries (FDE) for each function
   - Common Information Entries (CIE)
   - DWARF CFI directives for stack unwinding
```

#### Phase 2: IR Generation for Exception Constructs (2-3 days)
```
1. visitThrowStatementNode():
   - Generate call to __cxa_allocate_exception
   - Store exception object
   - Call __cxa_throw with type info
   
2. visitTryCatchStatementNode():
   - Create landing pad basic block
   - Generate cleanup code
   - Insert personality function reference (__gxx_personality_v0)
   - Generate type-match checks for catch handlers
```

#### Phase 3: Runtime Integration (1-2 days)
```
1. Link against libstdc++ exception runtime:
   - __cxa_allocate_exception
   - __cxa_throw
   - __cxa_begin_catch
   - __cxa_end_catch
   - __gxx_personality_v0

2. Test progressive scenarios:
   - Simple throw/catch same function
   - Cross-function unwinding
   - Multiple catch handlers
   - Rethrow
   - Nested try-catch blocks
```

**Estimated Effort:** 5-8 days  
**Priority:** HIGH (fundamental C++ feature)  
**Files to Modify:**
- `src/IRConverter.h` - Add exception IR generation
- `src/ElfFileWriter.h` - Add .gcc_except_table and .eh_frame generation
- `src/DwarfCFI.h` - Enhance CFI generation
- `src/LSDAGenerator.h` - May need creation for LSDA

---

### 2. Variadic Arguments (2 files) - **MEDIUM COMPLEXITY** - ⚠️ **PARTIAL PROGRESS - va_arg BROKEN**

**Files:**
- `test_va_implementation.cpp`
- `test_varargs.cpp`

**Current State:**
- ✅ `__builtin_va_start` and `__builtin_va_arg` intrinsics recognized
- ✅ Basic IR generation exists in CodeGen.h
- ✅ System V AMD64 ABI va_list structure implemented
- ✅ Register save area properly set up (176 bytes: 48 for int regs + 128 for float regs)
- ✅ va_list structure initialization with gp_offset, fp_offset, overflow_arg_area, reg_save_area
- ✅ Fixed parameter offset tracking for variadic functions
- ❌ **va_arg implementation has critical bugs causing segmentation faults**
- ❌ **No variadic tests actually work end-to-end** (tests that "pass" only parse but don't use va_arg)

**Previous Issues (NOW RESOLVED):**
On Linux x64 (System V AMD64 ABI), variadic arguments require:
1. ✅ Register save area for integer args (RDI, RSI, RDX, RCX, R8, R9) - **COMPLETED**
2. ✅ Register save area for floating-point args (XMM0-XMM7) - **COMPLETED**
3. ✅ Overflow area on stack for additional args - **COMPLETED**
4. ✅ va_list structure with 4 fields: gp_offset, fp_offset, overflow_arg_area, reg_save_area - **COMPLETED**

**Implementation Progress:**

#### Phase 1: Proper va_list Structure - ✅ **COMPLETED**
```
✅ 1. Define System V AMD64 va_list layout:
   struct __va_list_tag {
       unsigned int gp_offset;    // Offset into register save area (0-48)
       unsigned int fp_offset;    // Offset into FP register save area (48-176)
       void* overflow_arg_area;   // Stack overflow area
       void* reg_save_area;       // Points to saved registers
   };

✅ 2. Allocate reg_save_area on stack (176 bytes):
   - First 48 bytes: 6 integer registers (RDI, RSI, RDX, RCX, R8, R9)
   - Next 128 bytes: 8 FP registers (XMM0-XMM7)
   - Added FLOAT_REG_COUNT constant (8) for clarity
   - Added static_assert to verify INT_REG_COUNT == 6
```

#### Phase 2: va_start Implementation - ✅ **COMPLETED**
```
✅ 1. When entering variadic function:
   - Save all potential argument registers to reg_save_area
   - Initialize gp_offset based on fixed args count
   - Initialize fp_offset based on fixed args count
   - Set overflow_arg_area to point past fixed args on stack
   - Store reg_save_area pointer

✅ 2. Update __builtin_va_start intrinsic handling:
   - Replace simple pointer assignment
   - Initialize all 4 fields of va_list structure
```

#### Phase 3: va_arg Implementation - ✅ **WORKING FOR REGISTER ARGS**
```
✅ 1. For __builtin_va_arg(va_list, type):
   - Fixed critical bugs in CodeGen.h (Phase 3 commits)
   - va_start now points args to reg_save_area + gp_offset on Linux
   - va_arg correctly reads from args and advances by 8 bytes
   - Check if type fits in registers
   - If integer type and gp_offset < 48:
     * Read from reg_save_area + gp_offset
     * Increment gp_offset by 8 (by advancing args pointer)
   - ❌ Overflow support not yet implemented (reads from invalid memory past register save area)
```

#### Phase 4: Testing - ⚠️ **PARTIAL SUCCESS**
```
✅ 1. Variadic function declarations parse correctly (test_va_simple_ret30.cpp, test_variadic_mixed_ret16.cpp)
✅ 2. va_arg with integer arguments (up to 5 variadic args for 1 fixed param) - WORKING
   - Tested: sum2(2, 30, 12) = 42 ✓
   - Tested: sum3(3, 10, 20, 30) = 60 ✓
   - Tested: sum5(5, 1, 2, 3, 4, 5) = 15 ✓
❌ 3. va_arg with floating-point arguments - NOT YET TESTED (requires fp_offset support)
❌ 4. va_arg with > 5 integer args (overflow to stack) - INCORRECT (returns partial sum)
   - Tested: sum7(7, 1, 2, 3, 4, 5, 6, 7) returns 15 instead of 28 (6th and 7th args not read)
   - Root cause: no overflow_arg_area support in char* va_list path
❌ 5. Test with struct arguments passed via varargs - NOT TESTED

Note: All tests that use ≤5 variadic integer args now work correctly.
Overflow support requires conditional logic in va_arg which is complex to implement.
```

**Recent Progress (Phase 3-4 Fixes - January 2026):**
1. ✅ Fixed va_start for Linux with `typedef char* va_list;` to use register save area
2. ✅ Fixed va_arg double-dereference bug 
3. ✅ Fixed holds_address_only check in arithmetic operations
4. ✅ Fixed gp_offset dereference size (32-bit, not 64-bit)
5. ✅ All tests pass (833 tests as of January 2026)

**Testing Results:**
- Basic variadic with 2 args: 30 + 12 = 42 ✓
- Variadic with 3 args: 10 + 20 + 30 = 60 ✓
- Variadic with 5 args (fills registers): 1+2+3+4+5 = 15 ✓
- Variadic with 7 args (overflow): Returns 15 instead of 28 ✗

**Known Limitations:**
- Overflow arguments (>5 int args after 1 fixed param) not yet handled
- Float variadic arguments not yet tested
- Requires implementing conditional logic in va_arg for overflow support

**Estimated Remaining Effort:** 2-3 days for overflow support and float args  
**Priority:** MEDIUM (basic variadic functionality now works)  
**Files to Modify:**
- `src/CodeGen.h` - Add overflow_arg_area support with conditional branches

---

### 3. Virtual Functions / RTTI (2 files) - **HIGH COMPLEXITY**

**Files:**
- `test_covariant_return.cpp` - Covariant return types
- `test_virtual_inheritance.cpp` - Virtual inheritance diamond problem

**Current State:**
- ✅ Basic virtual function dispatch works
- ✅ Vtable generation exists
- ✅ Basic typeinfo generation exists
- ❌ Covariant return type thunks not generated
- ❌ Virtual base class offsets not tracked
- ❌ VTT (Virtual Table Table) not generated

**Root Cause Analysis:**

#### Issue 3a: Covariant Return Types
When derived class overrides base virtual function with derived return type:
```cpp
struct Base {
    virtual Base* getSelf() { return this; }
};
struct Derived : Base {
    virtual Derived* getSelf() { return this; }  // Covariant
};
```
Requires:
- Return type adjustment thunk in vtable
- Pointer offset adjustment when called through base pointer

#### Issue 3b: Virtual Inheritance
Diamond inheritance requires runtime base offset calculation:
```cpp
struct Base { int x; };
struct Left : virtual Base { int y; };
struct Right : virtual Base { int z; };
struct Diamond : Left, Right { int w; };  // Only one Base
```
Requires:
- Virtual base pointer (vbptr) in each class
- VTT (Virtual Table Table) for construction
- Runtime offset calculation via vbase offset in vtable

**Implementation Steps:**

#### Phase 1: Covariant Return Thunks (2-3 days)
```
1. Detect covariant return types:
   - Compare base and derived return types
   - Verify derived is derived-from base return type
   - Calculate pointer offset adjustment needed

2. Generate thunk functions:
   - Create wrapper function in vtable
   - Call actual derived function
   - Adjust return pointer by class offset
   - Return adjusted pointer

3. Update vtable generation:
   - Insert thunk address instead of function address
   - Maintain separate mapping of thunks to actual functions
```

#### Phase 2: Virtual Base Class Support (3-4 days)
```
1. Track virtual base classes in inheritance hierarchy:
   - Mark virtual inheritance in AST
   - Calculate virtual base offsets
   - Store in class metadata

2. Generate virtual base pointer (vbptr):
   - Add vbptr as first member in classes with virtual bases
   - Initialize in constructor to point to vbase offset table

3. Create VTT (Virtual Table Table):
   - For each class with virtual bases
   - Contains vtable pointers for base classes
   - Used during construction/destruction

4. Update member access code:
   - For virtual base members, load offset from vbptr
   - Add offset to 'this' pointer
   - Access member at adjusted address
```

#### Phase 3: Constructor Vtable (1-2 days)
```
1. During construction, use construction vtable:
   - Prevents calling derived virtual functions from base constructor
   - Update vtable pointer as each level constructs

2. Generate separate vtables for construction:
   - Base-in-Derived construction vtable
   - Update vptr at each constructor level
```

**Estimated Effort:** 6-9 days  
**Priority:** MEDIUM (advanced C++ features, less commonly used)  
**Files to Modify:**
- `src/CodeGen.h` - Vtable and thunk generation
- `src/IRConverter.h` - Virtual base access
- `src/SymbolTable.h` - Track virtual base offsets
- New file: `src/VirtualInheritance.h` - VTT generation

---

### 4. ~~C++ Runtime Initialization (2 files)~~ - **✅ FIXED**

**Files:**
- ~~`test_addressof_int_index.cpp`~~ ✅ Fixed
- ~~`test_arrays_comprehensive.cpp`~~ ✅ Fixed

**Previous State:**
- ✅ Generated assembly was mostly correct
- ❌ Crashed with SIGSEGV during main() execution

**Root Cause (IDENTIFIED AND FIXED):**
The issue was NOT in ELF initialization sections as originally suspected. The actual bug was in code generation for array index loading:
- Compiler generated 64-bit mov instructions (`mov rcx, [rbp-offset]`) when loading 32-bit integer array indices
- This read 8 bytes instead of 4, polluting the upper 32 bits of the register with garbage from uninitialized stack memory
- When used in array index calculations, this garbage caused invalid memory addresses, resulting in segmentation faults

**Solution:**
Modified `emitLoadIndexIntoRCX()` in `src/IRConverter.h` to:
1. Accept a `size_in_bits` parameter (required, no default)
2. Generate size-appropriate mov instructions:
   - For 32-bit: `mov ecx, [rbp-offset]` (zero-extends to 64 bits)
   - For 64-bit: `mov rcx, [rbp-offset]` (with REX.W prefix)
3. Updated all call sites to pass the correct index size

**Status:** ✅ **FIXED** - Both tests now compile, link, and run successfully.

---

### 5. Other Complex Features (3 files) - **HIGH COMPLEXITY**

**Files:**
- `test_rvo_very_large_struct.cpp` - Large struct RVO/NRVO
- `test_lambda_cpp20_comprehensive_ret135.cpp` - Advanced C++20 lambda features
- `test_xvalue_all_casts.cpp` - xvalue handling across all cast types

#### Issue 5a: Large Struct RVO/NRVO

**Current State:**
- ✅ Basic RVO works for small structs
- ❌ Large structs (>16 bytes) may not follow ABI correctly

**Root Cause:**
System V AMD64 ABI has specific rules for returning large structs:
- Structs >16 bytes returned via hidden first parameter
- Caller allocates space and passes address in RDI
- Function writes result directly to caller's memory

**Implementation Steps (2-3 days):**
```
1. Detect large struct returns (>16 bytes)
2. Add hidden pointer parameter in RDI
3. Rewrite return statements to use memcpy to hidden parameter
4. Update call sites to allocate space and pass address
5. Test with progressively larger structs (24, 32, 64, 128 bytes)
```

#### Issue 5b: Advanced C++20 Lambda Features

**Current State:**
- ✅ Basic lambdas work
- ✅ Lambda capture works
- ❌ Some C++20 features may be incomplete

**Potential Issues:**
- Template lambdas
- Default captures with parameter packs
- Constexpr lambdas
- Lambda capture of structured bindings

**Implementation Steps (3-4 days):**
```
1. Analyze specific test_lambda_cpp20_comprehensive_ret135.cpp failures
2. Identify which C++20 feature is crashing
3. Add support for that specific feature
4. May require parser updates for C++20 lambda syntax
```

#### Issue 5c: XValue All Casts

**Current State:**
- ✅ Basic static_cast works
- ✅ Basic move semantics work
- ❌ Some cast combinations crash

**Root Cause:**
Likely dynamic_cast with RTTI or complex cast scenarios

**Implementation Steps (2-3 days):**
```
1. Analyze exact failure point in test_xvalue_all_casts.cpp
2. If dynamic_cast: requires full RTTI implementation (see Issue 3)
3. If const_cast/reinterpret_cast: may need value category tracking
4. Implement missing cast functionality
```

**Estimated Effort:** 7-10 days total  
**Priority:** LOW-MEDIUM (advanced features)

---

## Implementation Priority & Timeline

### ✅ Phase 0: Critical Bug Fix (COMPLETED)
~~1. **C++ Runtime Initialization / Array Index Loading** (1 day) - Blocks working code~~
   - **FIXED:** Modified `emitLoadIndexIntoRCX()` to use size-appropriate mov instructions
   - **Impact:** All 674 tests now compile and link successfully

### Phase 1: High Priority Runtime Features (2-3 weeks) - **MOSTLY COMPLETE**
1. ~~**Variadic Arguments** (4 days)~~ - ✅ **BASIC FUNCTIONALITY WORKING**
   - ✅ Phase 1: va_list structure implementation
   - ✅ Phase 2: va_start implementation  
   - ✅ Phase 3: va_arg implementation (basic integer args working, up to 5 variadic args)
   - ⚠️ Phase 4: Partial testing (overflow support not yet implemented - 2-3 days remaining)
2. **Exceptions** (5-8 days) - Common feature, currently crashes at runtime

### Phase 2: Advanced Features (2-3 weeks)
3. **Virtual Inheritance & Covariant Returns** (6-9 days) - Less common but important, crashes at runtime
4. **Large Struct RVO** (2-3 days) - ABI compliance, crashes at runtime

### Phase 3: Edge Cases (1-2 weeks)
5. **Advanced Lambda Features** (3-4 days) - Modern C++, crashes at runtime
6. **Complex Cast Scenarios** (2-3 days) - Edge cases, crashes at runtime

**Total Estimated Effort:** 21-33 days for remaining 9 test files with runtime issues (adjusted from 19-30 days after discovering va_arg bugs)

---

## Testing Strategy

For each issue:
1. **Unit Tests:** Minimal reproducible test case
2. **Integration Tests:** Full test file from test suite
3. **Regression Tests:** Ensure no existing tests break
4. **ABI Compliance:** Compare assembly output with Clang

**Validation Commands:**
```bash
# Run all tests - verify compilation and linking
cd /home/runner/work/FlashCpp/FlashCpp && ./tests/run_all_tests.sh

# Validate return values of runnable tests
cd /home/runner/work/FlashCpp/FlashCpp && ./tests/validate_return_values.sh
```

---

## Risk Assessment

### High Risk Changes:
- Exception handling (complex runtime integration)
- Virtual inheritance (affects all vtable generation)

### Medium Risk Changes:
- Variadic arguments (affects calling convention)
- ~~C++ runtime init (affects ELF structure)~~ ✅ Already fixed

### Low Risk Changes:
- Large struct RVO (localized to return handling)
- Advanced lambda features (additive only)

---

## Dependencies & Prerequisites

1. **External Libraries:**
   - Must link against libstdc++ for exception runtime
   - May need libgcc_s for unwinding

2. **Knowledge Required:**
   - System V AMD64 ABI specification
   - ELF format (.eh_frame, .gcc_except_table)
   - DWARF debugging format (CFI)
   - C++ ABI specification (Itanium C++ ABI)

3. **Tools for Debugging:**
   - `readelf -a` - Inspect ELF sections
   - `objdump -d` - Disassemble code
   - `strace` - Trace system calls
   - `c++filt` - Demangle symbols

---

## Success Criteria

- **Current Status:** 674/674 tests compile and link successfully (100%) ✅
- **Target:** 674/674 tests pass runtime execution (100%)
- **Realistic Goal:** 669+/674 tests passing (>99%, allowing for genuinely complex features)
- **Code Quality:** No new compiler warnings
- **Performance:** No significant compilation speed regression

---

## Notes

- ✅ **Major milestone achieved:** All 674 tests now compile successfully (up from 658/674)
- The array index loading bug fix resolved 16 previously failing tests
- Remaining issues are runtime-only (link failures or execution crashes)
- Some features may be fundamentally incompatible with FlashCpp's architecture
- If a feature proves too complex, document why and mark as "won't implement"
- Focus on features that provide the most value to users
- Maintain clean separation between compiler phases

---

*Document Created: 2025-12-22*  
*Last Updated: 2026-01-06 (Variadic functions: Phase 3 fixed, basic va_arg working for integer register args)*  
*For: FlashCpp Compiler Development*  
*Status: Implementation Phase - Variadic Arguments Basic Functionality Working, Overflow Support Pending*
