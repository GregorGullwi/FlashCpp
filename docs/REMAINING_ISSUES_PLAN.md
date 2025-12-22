# Plan to Fix Remaining Issues in FlashCpp

**Current Status:** 658/674 tests passing (97.6%)  
**Remaining:** 11 runtime crashes  
**Date:** 2025-12-22

---

## Overview

This document outlines a comprehensive plan to fix the 11 remaining runtime crashes in FlashCpp. The issues are categorized by type with specific implementation steps, complexity estimates, and dependencies.

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

### 2. Variadic Arguments (2 files) - **MEDIUM COMPLEXITY**

**Files:**
- `test_va_implementation.cpp`
- `test_varargs.cpp`

**Current State:**
- ✅ `__builtin_va_start` and `__builtin_va_arg` intrinsics recognized
- ✅ Basic IR generation exists in CodeGen.h
- ❌ System V AMD64 ABI va_list implementation incomplete
- ❌ Register save area not properly set up

**Root Cause:**
On Linux x64 (System V AMD64 ABI), variadic arguments require:
1. Register save area for integer args (RDI, RSI, RDX, RCX, R8, R9)
2. Register save area for floating-point args (XMM0-XMM7)
3. Overflow area on stack for additional args
4. va_list structure with 4 fields: gp_offset, fp_offset, overflow_arg_area, reg_save_area

**Implementation Steps:**

#### Phase 1: Proper va_list Structure (1 day)
```
1. Define System V AMD64 va_list layout:
   struct __va_list_tag {
       unsigned int gp_offset;    // Offset into register save area (0-48)
       unsigned int fp_offset;    // Offset into FP register save area (48-176)
       void* overflow_arg_area;   // Stack overflow area
       void* reg_save_area;       // Points to saved registers
   };

2. Allocate reg_save_area on stack (176 bytes):
   - First 48 bytes: 6 integer registers (RDI, RSI, RDX, RCX, R8, R9)
   - Next 128 bytes: 8 FP registers (XMM0-XMM7)
```

#### Phase 2: va_start Implementation (1 day)
```
1. When entering variadic function:
   - Save all potential argument registers to reg_save_area
   - Initialize gp_offset based on fixed args count
   - Initialize fp_offset based on fixed args count
   - Set overflow_arg_area to point past fixed args on stack
   - Store reg_save_area pointer

2. Update __builtin_va_start intrinsic handling:
   - Replace simple pointer assignment
   - Initialize all 4 fields of va_list structure
```

#### Phase 3: va_arg Implementation (1 day)
```
1. For __builtin_va_arg(va_list, type):
   - Check if type fits in registers
   - If integer type and gp_offset < 48:
     * Read from reg_save_area + gp_offset
     * Increment gp_offset by 8
   - If floating type and fp_offset < 176:
     * Read from reg_save_area + fp_offset
     * Increment fp_offset by 16
   - Otherwise:
     * Read from overflow_arg_area
     * Increment overflow_arg_area by aligned size
```

#### Phase 4: Testing (1 day)
```
1. Test with integer arguments only
2. Test with floating-point arguments
3. Test with mixed arguments
4. Test with > 6 integer or > 8 FP arguments (overflow)
5. Test with struct arguments passed via varargs
```

**Estimated Effort:** 4 days  
**Priority:** MEDIUM-HIGH (common C++ feature)  
**Files to Modify:**
- `src/IRConverter.h` - Fix va_start/va_arg implementation
- `src/CodeGen.h` - May need ABI-specific register handling

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

### 4. C++ Runtime Initialization (2 files) - **MEDIUM COMPLEXITY**

**Files:**
- `test_addressof_int_index.cpp`
- `test_arrays_comprehensive.cpp`

**Current State:**
- ✅ Generated assembly is correct (verified via objdump)
- ❌ Crashes with SIGSEGV before entering main()
- Issue is NOT in compiler code generation

**Root Cause:**
Missing or incorrect ELF initialization sections cause C++ runtime to crash before main():
1. `.init_array` section missing or malformed
2. Global constructor registration incomplete
3. TLS (Thread Local Storage) initialization missing
4. .ctors/.dtors section compatibility

**Implementation Steps:**

#### Phase 1: Diagnostic (1 day)
```
1. Compare generated ELF with working Clang output:
   readelf -a test_addressof_int_index.obj (FlashCpp)
   readelf -a test_addressof_int_index_clang.obj (Clang)

2. Check for missing sections:
   - .init_array / .fini_array
   - .ctors / .dtors
   - .preinit_array
   - .tdata / .tbss (for TLS)

3. Verify dynamic symbol table entries
4. Check relocation entries for initialization functions
```

#### Phase 2: Add Missing ELF Sections (1-2 days)
```
1. Generate .init_array section:
   - Contains function pointers to run before main()
   - Must be properly aligned (8 bytes on x64)
   - Add terminating NULL entry

2. Generate .fini_array section:
   - Contains function pointers to run after main()
   - For destructors of global objects

3. If static constructors exist:
   - Register them in .init_array
   - Generate stub functions if needed
```

#### Phase 3: Fix Dynamic Linking (1 day)
```
1. Ensure proper symbol visibility:
   - Mark necessary symbols as GLOBAL
   - Mark internal symbols as LOCAL
   - Proper symbol binding (STB_GLOBAL, STB_WEAK)

2. Add missing dynamic entries:
   - DT_INIT / DT_FINI
   - DT_INIT_ARRAY / DT_INIT_ARRAYSZ
   - DT_FINI_ARRAY / DT_FINI_ARRAYSZ

3. Verify .dynamic section is complete
```

#### Phase 4: Testing (1 day)
```
1. Test with simple main-only programs
2. Test with global variables
3. Test with array initialization
4. Test with static constructors
5. Use strace to identify exact crash point
```

**Estimated Effort:** 4-5 days  
**Priority:** HIGH (blocks otherwise working code)  
**Files to Modify:**
- `src/ElfFileWriter.h` - Add initialization sections
- `src/ObjectFileCommon.h` - May need section definitions

---

### 5. Other Complex Features (3 files) - **HIGH COMPLEXITY**

**Files:**
- `test_rvo_very_large_struct.cpp` - Large struct RVO/NRVO
- `test_lambda_cpp20_comprehensive.cpp` - Advanced C++20 lambda features
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
1. Analyze specific test_lambda_cpp20_comprehensive.cpp failures
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

### Phase 1: Critical Issues (2-3 weeks)
1. **C++ Runtime Initialization** (4-5 days) - Blocks working code
2. **Variadic Arguments** (4 days) - Common feature
3. **Exceptions** (5-8 days) - Fundamental C++ feature

### Phase 2: Advanced Features (2-3 weeks)
4. **Virtual Inheritance & Covariant Returns** (6-9 days) - Less common but important
5. **Large Struct RVO** (2-3 days) - ABI compliance

### Phase 3: Edge Cases (1-2 weeks)
6. **Advanced Lambda Features** (3-4 days) - Modern C++
7. **Complex Cast Scenarios** (2-3 days) - Edge cases

**Total Estimated Effort:** 6-8 weeks for complete implementation

---

## Testing Strategy

For each issue:
1. **Unit Tests:** Minimal reproducible test case
2. **Integration Tests:** Full test file from test suite
3. **Regression Tests:** Ensure no existing tests break
4. **ABI Compliance:** Compare assembly output with Clang

**Validation Command:**
```bash
cd /home/runner/work/FlashCpp/FlashCpp && ./tests/validate_return_values.sh
```

---

## Risk Assessment

### High Risk Changes:
- Exception handling (complex runtime integration)
- Virtual inheritance (affects all vtable generation)

### Medium Risk Changes:
- Variadic arguments (affects calling convention)
- C++ runtime init (affects ELF structure)

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

- **Target:** 674/674 tests passing (100%)
- **Minimum:** 669/674 tests passing (>99%, allow 5 for genuinely impossible features)
- **Code Quality:** No new compiler warnings
- **Performance:** No significant compilation speed regression

---

## Notes

- Some features may be fundamentally incompatible with FlashCpp's architecture
- If a feature proves too complex, document why and mark as "won't implement"
- Focus on features that provide the most value to users
- Maintain clean separation between compiler phases

---

*Document Created: 2025-12-22*  
*For: FlashCpp Compiler Development*  
*Status: Planning Phase*
