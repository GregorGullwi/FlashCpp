# Plan to Fix Remaining Issues in FlashCpp

**Current Status:** 674/674 tests compile and link successfully (100%)  
**Runtime Status:** 663/674 tests execute successfully (98.4%)  
**Remaining:** 11 runtime crashes (SIGSEGV before or during main())  
**Last Updated:** 2025-12-22

---

## Overview

FlashCpp successfully compiles and links all 674 test files. However, 11 tests crash at runtime with SIGSEGV (signal 11). This document outlines the root causes and implementation plans to fix these crashes.

---

## Runtime Crash Summary

The following 11 tests crash at runtime with SIGSEGV (signal 11):

**Category 1: C++ Runtime Initialization (2 crashes)**
- `test_addressof_int_index.cpp` - Crashes before main()
- `test_arrays_comprehensive.cpp` - Crashes before main()

**Category 2: Exceptions (2 crashes)**
- `test_exceptions_basic.cpp` - Missing exception handling runtime
- `test_exceptions_nested.cpp` - Missing exception handling runtime

**Category 3: Variadic Arguments (2 crashes)**
- `test_va_implementation.cpp` - Incomplete System V AMD64 ABI va_list
- `test_varargs.cpp` - Missing external C helper functions + incomplete va_list

**Category 4: Virtual/RTTI (2 crashes)**
- `test_covariant_return.cpp` - Missing covariant return thunks + RTTI
- `test_virtual_inheritance.cpp` - Missing virtual base support + RTTI

**Category 5: Advanced Features (3 crashes)**
- `test_rvo_very_large_struct.cpp` - Large struct ABI compliance issue
- `test_lambda_cpp20_comprehensive.cpp` - Advanced C++20 lambda features
- `test_xvalue_all_casts.cpp` - Complex cast scenarios

---

## Detailed Issue Analysis & Implementation Plans

### 1. Exceptions (2 files) - **MEDIUM-HIGH COMPLEXITY**

**Status:** ✅ Parsing complete | ❌ IR generation missing | ❌ Runtime support missing

**Root Cause:** No exception handling runtime support (stack unwinding, exception tables, DWARF CFI).

**Implementation (5-8 days):**
1. Generate .gcc_except_table section with LSDA
2. Enhance .eh_frame with FDE/CIE entries
3. Implement visitThrowStatementNode() - call __cxa_allocate_exception, __cxa_throw
4. Implement visitTryCatchStatementNode() - landing pads, personality function
5. Link against libstdc++ exception runtime

**Files:** `src/IRConverter.h`, `src/ElfFileWriter.h`, `src/DwarfCFI.h`, `src/LSDAGenerator.h`

---

### 2. Variadic Arguments (2 files) - **MEDIUM COMPLEXITY**

**Status:** ✅ Intrinsics recognized | ⚠️ Basic IR exists | ❌ System V AMD64 ABI incomplete

**Root Cause:** System V AMD64 ABI requires 176-byte register save area with va_list structure (gp_offset, fp_offset, overflow_arg_area, reg_save_area).

**Implementation (4 days):**
1. Define proper va_list structure (4 fields)
2. Allocate 176-byte reg_save_area (48 bytes GP + 128 bytes FP)
3. Fix __builtin_va_start - initialize all fields
4. Fix __builtin_va_arg - handle register vs stack overflow paths
5. Test with integer, FP, mixed, and overflow arguments

**Files:** `src/IRConverter.h`, `src/CodeGen.h`

---

### 3. Virtual Functions / RTTI (2 files) - **HIGH COMPLEXITY**

**Status:** ✅ Basic vtables work | ❌ Covariant thunks missing | ❌ Virtual base offsets missing

**Root Cause:**  
- Covariant returns need pointer adjustment thunks
- Virtual inheritance needs VTT and runtime offset calculation

**Implementation (6-9 days):**
1. Detect covariant return types and generate thunk functions
2. Track virtual base classes and calculate offsets
3. Generate vbptr in classes with virtual bases
4. Create VTT for virtual inheritance
5. Update member access for virtual base members
6. Generate construction vtables

**Files:** `src/CodeGen.h`, `src/IRConverter.h`, `src/SymbolTable.h`, `src/VirtualInheritance.h` (new)

---

### 4. C++ Runtime Initialization (2 files) - **MEDIUM COMPLEXITY** ⚠️ HIGH PRIORITY

**Status:** ✅ Assembly correct | ❌ Crashes before main() (SIGSEGV)

**Root Cause:** ELF initialization sections may be missing or malformed. Crash occurs during C++ runtime init before main() is called.

**Diagnostic Findings (2025-12-22):**
- Both clang and FlashCpp executables have identical .init_array/.fini_array contents
- Both have same program headers and sections
- Crash happens immediately after dynamic linker transfers control
- Main function address (0x401110) is correct in both
- frame_dummy (0x401100) is correctly referenced in .init_array

**Needs Further Investigation:**
- Possible stack alignment issue (FlashCpp allocates 0x140 bytes vs clang's smaller frame)
- May need to check .eh_frame format compatibility
- Verify all dynamic symbol bindings are correct
- Check if heap corruption during global initialization

**Implementation (4-5 days):**
1. Deep diagnostic: Compare ELF files byte-by-byte (readelf, objdump)
2. Check .init_array registration and format
3. Verify .dynamic section completeness (DT_INIT, DT_INIT_ARRAY, etc.)
4. Test with simple main-only programs first
5. Use strace/gdb to pinpoint exact crash instruction

**Files:** `src/ElfFileWriter.h`, `src/ObjectFileCommon.h`, `src/CodeGen.h`

**NOTE:** This issue blocks otherwise correct code and should be prioritized.

---

### 5. Advanced Features (3 files) - **MEDIUM-HIGH COMPLEXITY**

**Issue 5a: Large Struct RVO (test_rvo_very_large_struct.cpp)**
- Problem: Structs >16 bytes need hidden pointer parameter (System V AMD64 ABI)
- Solution: Detect large returns, add hidden RDI parameter, rewrite return to memcpy
- Effort: 2-3 days

**Issue 5b: C++20 Lambdas (test_lambda_cpp20_comprehensive.cpp)**
- Problem: Missing advanced C++20 lambda features (template lambdas, constexpr, etc.)
- Solution: Analyze specific failure, implement missing feature
- Effort: 3-4 days

**Issue 5c: Complex Casts (test_xvalue_all_casts.cpp)**
- Problem: Likely dynamic_cast with RTTI or complex cast scenarios
- Solution: Implement missing cast functionality
- Effort: 2-3 days

**Files:** `src/IRConverter.h`, `src/CodeGen.h`, `src/Parser.h`

---

## Implementation Priority & Timeline

### Phase 1: Critical Blocking Issues (1-2 weeks)
1. **C++ Runtime Initialization** (4-5 days) - **HIGH PRIORITY** - Blocks otherwise correct code
2. **Variadic Arguments** (4 days) - Common C feature

### Phase 2: Core C++ Features (2-3 weeks)  
3. **Exceptions** (5-8 days) - Fundamental C++ feature
4. **Large Struct RVO** (2-3 days) - ABI compliance

### Phase 3: Advanced Features (2-3 weeks)
5. **Virtual Inheritance & Covariant Returns** (6-9 days) - Advanced OOP
6. **Advanced Lambda Features** (3-4 days) - Modern C++20
7. **Complex Cast Scenarios** (2-3 days) - Edge cases

**Total Estimated Effort:** 6-8 weeks for complete implementation

---

## Testing & Validation

**Current Validation:** `./tests/validate_return_values.sh`

**Test Results (2025-12-22):**
- Total files: 674
- Compile success: 674/674 (100%)
- Link success: 674/674 (100%)
- Runtime success: 663/674 (98.4%)
- Runtime crashes: 11 (SIGSEGV signal 11)

**For Each Fix:**
1. Compile test file with FlashCpp
2. Link with clang++ linker
3. Run and verify exit code
4. Compare assembly with clang output (objdump -d)
5. Verify no regressions in passing tests

---

## Tools & Resources

**Required Knowledge:**
- System V AMD64 ABI specification
- ELF format (.eh_frame, .gcc_except_table)
- DWARF debugging format (CFI)
- Itanium C++ ABI specification

**Debugging Tools:**
- `readelf -a` - Inspect ELF sections
- `objdump -d` - Disassemble code
- `strace` - Trace system calls
- `c++filt` - Demangle symbols
- `LD_DEBUG=all` - Dynamic linker debugging

---

## Success Criteria

- **Target:** 674/674 tests running successfully (100%)
- **Minimum:** 669/674 tests passing (>99%)
- **Code Quality:** No new compiler warnings
- **Performance:** No significant compilation speed regression

---

## Investigation Log

### 2025-12-22: Initial Diagnostic

**Test Status Confirmed:**
- All 674 tests compile and link successfully
- 11 tests crash at runtime with SIGSEGV
- Crashes occur before or during main() execution

**C++ Runtime Init Investigation:**
- Compared `test_addressof_int_index` compiled by FlashCpp vs Clang
- ELF sections appear identical (program headers, .init_array, .fini_array)
- Dynamic linker successfully loads all libraries
- Crash occurs immediately after "transferring control"
- Main function address (0x401110) is correct
- frame_dummy (0x401100) correctly in .init_array

**Key Findings:**
- FlashCpp generates larger stack frames (0x140 bytes vs clang's smaller frames)
- All initialization appears correct on paper
- Issue may be subtle: stack alignment, heap corruption, or uninitialized data
- Need deeper investigation with byte-by-byte comparison

**Next Steps:**
- Focus on C++ Runtime Init issue (HIGH PRIORITY)
- Create minimal reproducible test case
- Use strace/gdb to find exact crash instruction
- Check for stack alignment issues in prologue/epilogue

---

*Document Created: 2025-12-22*  
*Last Updated: 2025-12-22*  
*Status: Investigation Phase - 11 runtime crashes identified*
