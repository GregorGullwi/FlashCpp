# Exception Handling Implementation Plan for FlashCpp

**Created**: 2025-12-30  
**Updated**: 2025-12-30  
**Status**: Phase-Based Implementation in Progress  
**Platform Targets**: Windows (MSVC SEH) and Linux (Itanium ABI)

## Executive Summary

This document consolidates the exception handling implementation plans for FlashCpp, combining insights from `EXCEPTION_HANDLING_IMPLEMENTATION.md`, `EXCEPTION_HANDLING_REMAINING_WORK.md`, and the RTTI implementation plan. Exception handling requires coordinated implementation across parsing, IR generation, code generation, and object file writing.

## Current Implementation Status

### ✅ Completed Components

| Component | Windows | Linux | Notes |
|-----------|---------|-------|-------|
| Parser support (try/catch/throw/noexcept) | ✅ | ✅ | Full syntax support |
| IR instructions (TryBegin, CatchBegin, Throw, etc.) | ✅ | ✅ | All opcodes defined |
| CodeGen visitors for exception nodes | ✅ | ✅ | Generates IR |
| DWARF CFI encoding utilities | N/A | ✅ | DwarfCFI.h |
| LSDA generator structure | N/A | ✅ | LSDAGenerator.h |
| .eh_frame section generation | N/A | ✅ | CIE/FDE with CFI |
| .gcc_except_table section | N/A | ✅ | LSDA structure |
| __cxa_throw call generation | N/A | ✅ | Works in IRConverter |
| External type_info symbols | N/A | ✅ | References runtime (_ZTIi, etc.) |
| is_catch_all flag in IR | N/A | ✅ | Explicit flag, not derived from type_index |
| Type table relocations (pcrel) | N/A | ✅ | R_X86_64_PC32 for type_info |
| FDE LSDA pointers | N/A | ✅ | All FDEs have LSDA field when CIE has 'L' |

### ⚠️ Fixed Issues (2025-12-30)

1. **✅ FIXED**: Last function in file (e.g., main) now properly gets try_blocks passed to exception info
2. **✅ FIXED**: Personality routine encoding changed from indirect to direct for non-PIE executables  
3. **✅ FIXED**: Type info now correctly uses actual Type enum (`_ZTIi` for int instead of `_ZTIv`)
4. **✅ FIXED**: is_catch_all detection now uses explicit IR flag instead of type_index==0
5. **✅ FIXED**: TType encoding changed to pcrel|sdata4 (0x1b) with R_X86_64_PC32 relocations
6. **✅ FIXED**: Type table entries now 4 bytes (sdata4) instead of 8 bytes (absptr)
7. **✅ FIXED**: All FDEs include LSDA pointer when CIE has 'L' augmentation
8. **✅ FIXED**: Built-in type_info symbols are external references to C++ runtime
9. **✅ FIXED**: Catch handler stack offset pre-computed during IR processing (crash fix)

### ❌ Known Issues

1. **Linux**: Runtime abort when executing - personality routine can't find landing pad
   - LSDA structure appears correct (call site table, action table, type table)
   - Call site offsets match function disassembly
   - Type table relocation is correct
   - **Next investigation**: Check if issue is in call site length or type matching
   
2. **Windows**: Code generation for SEH not implemented

3. **Both**: RTTI integration incomplete for complex exception type matching

## Architecture Overview

### Exception Handling Flow

```
throw expr;
    ↓
CodeGen: visitThrowStatementNode()
    ↓
IR: IrOpcode::Throw
    ↓
IRConverter: handleThrow()
    ↓
[Platform-Specific Code Generation]
    ↓
Windows: _CxxThrowException()     Linux: __cxa_throw()
    ↓                                    ↓
Runtime Library                   Runtime Library (libstdc++)
    ↓                                    ↓
Exception Tables (.xdata/.pdata)  Exception Tables (.eh_frame/.gcc_except_table)
    ↓                                    ↓
Find catch handler               Find catch handler
    ↓                                    ↓
Transfer to landing pad          Transfer to landing pad
```

### Key Files

| File | Purpose |
|------|---------|
| `src/IRTypes.h` | IR opcodes and operation structs |
| `src/CodeGen.h` | AST to IR conversion for exceptions |
| `src/IRConverter.h` | IR to machine code, exception handling |
| `src/ElfFileWriter.h` | Linux .eh_frame/.gcc_except_table |
| `src/ObjFileWriter.h` | Windows .pdata/.xdata (to be implemented) |
| `src/DwarfCFI.h` | DWARF CFI encoding utilities |
| `src/LSDAGenerator.h` | LSDA generation for Linux |

---

## Implementation Phases

### Phase 1: Fix Linux Exception Table Generation (HIGH PRIORITY)

**Goal**: Make exception throwing/catching work on Linux  
**Estimated Effort**: 3-5 days  
**Dependencies**: None

#### 1.1 Debug Current .eh_frame Issues

**Problem**: Some FDEs lack LSDA pointer, causing segmentation faults at runtime.

**Tasks**:
1. Analyze why `main()` function's FDE doesn't have augmentation data
2. Ensure ALL functions with try/catch get proper FDEs with LSDA pointers
3. Verify personality routine reference (`__gxx_personality_v0`) is correct

**Files to modify**:
- `src/ElfFileWriter.h`: `generate_eh_frame_fde()`, `add_function_exception_info()`
- `src/IRConverter.h`: Ensure try/catch blocks report proper offsets

#### 1.2 Fix Landing Pad Code Generation

**Problem**: Landing pad code exists but may not be properly integrated.

**Tasks**:
1. Verify `__cxa_begin_catch` and `__cxa_end_catch` calls are generated
2. Ensure exception value is properly extracted to catch variable
3. Test with simple int/pointer exception types first

**Files to modify**:
- `src/IRConverter.h`: `handleCatchBegin()`, `handleCatchEnd()`

#### 1.3 Add Call Site Table Tracking

**Problem**: Call site table needs accurate offsets for try regions.

**Tasks**:
1. Track actual code offsets during IR conversion
2. Ensure try_start_offset and try_length are accurate
3. Verify landing pad offsets point to correct code

**Files to modify**:
- `src/IRConverter.h`: Track offsets in handleTryBegin/handleTryEnd
- `src/ElfFileWriter.h`: Validate call site table generation

### Phase 2: RTTI Integration for Type Matching (MEDIUM PRIORITY)

**Goal**: Enable exception type matching beyond primitive types  
**Estimated Effort**: 4-6 days  
**Dependencies**: Phase 1

#### 2.1 Complete Type Info Symbol Generation

**Tasks**:
1. Generate `_ZTS*` (type string) symbols
2. Generate `_ZTI*` (type info) symbols for all catchable types
3. Handle class types with proper inheritance chains
4. Integrate with vtable RTTI pointers

**Files to modify**:
- `src/ElfFileWriter.h`: `create_type_string_symbol()`, `create_class_type_info()`
- `src/CodeGen.h`: Generate type info for exception types

#### 2.2 Implement Type Hierarchy for Catch Matching

**Tasks**:
1. Single inheritance type info (`__si_class_type_info`)
2. Multiple inheritance type info (`__vmi_class_type_info`)
3. Pointer and reference type info
4. CV-qualified type matching

**Files to modify**:
- `src/AstNodeTypes.h`: Ensure RTTI structures are complete
- `src/ElfFileWriter.h`: Generate proper type relationships

### Phase 3: Windows MSVC SEH Implementation (HIGH PRIORITY for Windows)

**Goal**: Implement Windows exception handling code generation  
**Estimated Effort**: 5-8 days  
**Dependencies**: None (parallel to Phase 1-2)

#### 3.1 .pdata and .xdata Section Generation

**Tasks**:
1. Generate RUNTIME_FUNCTION entries in .pdata
2. Generate UNWIND_INFO structures in .xdata
3. Link function entries to unwind information

**Structure reference**:
```cpp
struct RUNTIME_FUNCTION {
    DWORD BeginAddress;      // RVA of function start
    DWORD EndAddress;        // RVA of function end
    DWORD UnwindInfoAddress; // RVA of UNWIND_INFO
};

struct UNWIND_INFO {
    BYTE Version : 3;
    BYTE Flags : 5;           // UNW_FLAG_EHANDLER for C++ exceptions
    BYTE SizeOfProlog;
    BYTE CountOfCodes;
    BYTE FrameRegister : 4;
    BYTE FrameOffset : 4;
    UNWIND_CODE UnwindCode[1];
    // Handler data follows for UNW_FLAG_EHANDLER
};
```

**Files to modify**:
- `src/ObjFileWriter.h`: Add .pdata/.xdata section generation
- `src/IRConverter.h`: Track unwind codes during code generation

#### 3.2 C++ Exception Handler Data (FuncInfo)

**Tasks**:
1. Generate FuncInfo structure (magic number, try blocks, etc.)
2. Generate TryBlockMapEntry for each try block
3. Generate HandlerType for each catch handler
4. Generate UnwindMapEntry for stack unwinding

**Structure reference**:
```cpp
struct FuncInfo {
    DWORD magicNumber;      // 0x19930520 for x64
    DWORD maxState;
    DWORD pUnwindMap;
    DWORD nTryBlocks;
    DWORD pTryBlockMap;
    DWORD nIPMapEntries;
    DWORD pIPtoStateMap;
    DWORD pESTypeList;
    DWORD EHFlags;
};
```

#### 3.3 _CxxThrowException Integration

**Current status**: Call generation exists but incomplete metadata.

**Tasks**:
1. Complete ThrowInfo structure generation
2. Add CatchableTypeArray generation
3. Link to MSVC runtime library symbols

### Phase 4: noexcept and Exception Specifications (LOW PRIORITY)

**Goal**: Proper noexcept handling  
**Estimated Effort**: 2-3 days  
**Dependencies**: Phase 1-3

#### 4.1 noexcept Function Handling

**Tasks**:
1. Skip exception table generation for noexcept functions
2. Wrap noexcept function bodies in implicit terminate handler
3. Handle noexcept(expr) compile-time evaluation

**Files to modify**:
- `src/CodeGen.h`: Check noexcept during function processing
- `src/IRConverter.h`: Conditionally generate exception tables

### Phase 5: Advanced Features (FUTURE)

**Goal**: Complete exception handling feature parity  
**Estimated Effort**: 5-10 days  
**Dependencies**: Phase 1-4

#### 5.1 Stack Unwinding with Destructors

**Tasks**:
1. Track objects with destructors in each scope
2. Generate cleanup actions in unwind map
3. Call destructors in reverse construction order

#### 5.2 Rethrow Support

**Tasks**:
1. Verify __cxa_rethrow on Linux
2. Implement _CxxThrowException(NULL, NULL) on Windows
3. Test nested try/catch with rethrow

#### 5.3 Function-Try-Blocks

**Tasks**:
1. Parser support (may already exist)
2. Code generation for constructor/destructor function-try-blocks

---

## Testing Strategy

### Unit Tests

Create test files in `tests/`:

| Test File | Purpose |
|-----------|---------|
| `test_exceptions_basic.cpp` | Simple throw/catch of int |
| `test_exceptions_nested.cpp` | Nested try/catch blocks |
| `test_exceptions_types.cpp` | Multiple catch handlers, type matching |
| `test_exceptions_rethrow.cpp` | Rethrow current exception |
| `test_exceptions_class.cpp` | User-defined exception classes |
| `test_exceptions_hierarchy.cpp` | Catch base class exceptions |
| `test_exceptions_noexcept.cpp` | noexcept functions |

### Integration Tests

```bash
# Linux test workflow
./x64/Debug/FlashCpp tests/test_exceptions_basic.cpp -o test.o
g++ -no-pie -o test test.o -lstdc++
./test
echo "Exit code: $?"

# Windows test workflow (when implemented)
FlashCpp.exe tests\test_exceptions_basic.cpp -o test.obj
link test.obj kernel32.lib msvcrt.lib
test.exe
```

### Verification Tools

| Tool | Purpose | Command |
|------|---------|---------|
| readelf | Check ELF sections | `readelf -wf test.o` |
| objdump | Disassemble code | `objdump -d test.o` |
| dumpbin | Check COFF sections | `dumpbin /unwindinfo test.obj` |
| gdb | Debug exceptions | `gdb -ex "catch throw" ./test` |

---

## Implementation Notes

### Linux Exception Handling (Itanium ABI)

**Key References**:
- [Itanium C++ ABI Exception Handling](https://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html)
- [DWARF 4 Standard](http://dwarfstd.org/doc/DWARF4.pdf)
- [LSB Exception Frames](https://refspecs.linuxfoundation.org/LSB_3.0.0/LSB-Core-generic/LSB-Core-generic/ehframechpt.html)

**Runtime Functions**:
- `__cxa_allocate_exception` - Allocate exception object
- `__cxa_throw` - Throw exception
- `__cxa_begin_catch` - Start catch handler
- `__cxa_end_catch` - End catch handler
- `__cxa_rethrow` - Rethrow current exception
- `__gxx_personality_v0` - Personality routine

### Windows Exception Handling (MSVC SEH)

**Key References**:
- [x64 Exception Handling](https://docs.microsoft.com/en-us/cpp/build/exception-handling-x64)
- [UNWIND_INFO Structure](https://docs.microsoft.com/en-us/cpp/build/unwind-data-structure-definition)

**Runtime Functions**:
- `_CxxThrowException` - Throw exception
- `__CxxFrameHandler4` - Exception handler (VS2019+)
- `__CxxFrameHandler3` - Exception handler (older)
- `_CxxUnwindHelper` - Unwind stack

---

## Estimated Timeline

| Phase | Effort | Priority | Status |
|-------|--------|----------|--------|
| Phase 1: Fix Linux Exception Tables | 3-5 days | HIGH | 🔄 In Progress |
| Phase 2: RTTI Integration | 4-6 days | MEDIUM | ⏳ Pending |
| Phase 3: Windows MSVC SEH | 5-8 days | HIGH (Windows) | ⏳ Pending |
| Phase 4: noexcept | 2-3 days | LOW | ⏳ Pending |
| Phase 5: Advanced Features | 5-10 days | FUTURE | ⏳ Pending |

**Total Estimated Effort**: 19-32 days for complete implementation

---

## Files to Delete After Completion

These files should be consolidated into this plan and then removed:
- `docs/EXCEPTION_HANDLING_IMPLEMENTATION.md` - MSVC-specific guide (merged here)
- `docs/EXCEPTION_HANDLING_REMAINING_WORK.md` - Linux remaining work (merged here)

---

## Quick Start: Debugging Current Issues

### Step 1: Compile and Inspect

```bash
cd /tmp
/path/to/FlashCpp tests/test_exceptions_basic.cpp -o test.o
readelf -wf test.o  # Check .eh_frame
readelf -S test.o | grep except  # Check sections exist
```

### Step 2: Link and Run

```bash
g++ -no-pie -o test test.o -lstdc++ 2>&1
./test  # Will likely segfault
```

### Step 3: Debug with strace

```bash
strace ./test 2>&1 | tail -20
```

### Step 4: Check Generated Code

```bash
objdump -d test.o | less  # Look for __cxa_throw calls
readelf -s test.o | grep cxa  # Check exception symbols
```

---

## Appendix: Exception IR Instructions

```cpp
enum class IrOpcode {
    // Exception handling
    TryBegin,       // Begin try block: [label_for_handlers]
    TryEnd,         // End try block
    CatchBegin,     // Begin catch handler: [exception_var_temp, type_index, catch_end_label]
    CatchEnd,       // End catch handler
    Throw,          // Throw exception: [exception_temp, type_index]
    Rethrow,        // Rethrow current exception (throw; with no argument)
};

struct CatchBeginOp {
    TempVar exception_temp;
    TypeIndex type_index;
    bool is_reference;
    bool is_rvalue_reference;
    bool is_catch_all;  // For catch(...)
    StringHandle catch_end_label;
};

struct ThrowOp {
    TempVar exception_temp;
    TypeIndex type_index;
    size_t type_size;
    bool is_rvalue;
};
```
