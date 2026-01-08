# Exception Handling Implementation Plan for FlashCpp

**Created**: 2025-12-30  
**Updated**: 2026-01-08  
**Status**: Mixed (Linux: Advanced/Debugging, Windows: Partial Implementation)  
**Platform Targets**: Windows (MSVC SEH) and Linux (Itanium ABI)

## Executive Summary

This document consolidates the exception handling implementation plans for FlashCpp. Exception handling requires coordinated implementation across parsing, IR generation, code generation, and object file writing.

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
| .pdata section generation | ✅ | N/A | Generated in ObjFileWriter |
| .xdata section generation | ✅ | N/A | UNWIND_INFO and FuncInfo layout |
| _CxxThrowException call generation | ✅ | N/A | Basic call (ThrowInfo is NULL) |
| External type_info symbols | N/A | ✅ | References runtime (_ZTIi, etc.) |
| is_catch_all flag in IR | ✅ | ✅ | Explicit flag |
| Literal exception values | ✅ | ✅ | `throw 42` support |

### ⚠️ Fixed Issues (2025-12-30 -> 2026-01-08)

1. **✅ FIXED**: Last function in file (e.g., main) improved try_blocks handling.
2. **✅ FIXED**: Personality routine encoding fixes (Linux).
3. **✅ FIXED**: Type info naming repairs.
4. **✅ FIXED**: Windows `.pdata` and `.xdata` generation infrastructure implemented in `ObjFileWriter.h`.
5. **✅ FIXED**: Windows `_CxxThrowException` calls generated in `IRConverter`.

### ❌ Known Issues

1. **Linux**: Runtime crash/segfault when executing exception code (linking issue or landing pad misalignment).
   - "no .eh_frame_hdr table will be created" warning still reported.
2. **Windows**: 
   - `_ThrowInfo` structure passed to `_CxxThrowException` is currently `NULL`.
   - Result: Can throw exceptions, but `catch(type)` will likely fail or treat everything as catch-all/crash. Stack unwinding should work (destructors), but type matching needs `ThrowInfo`.
3. **Both**: RTTI integration incomplete for complex exception type matching (user-defined types need complete `type_info` generation).

### Next Steps to Fix Linux Runtime Issue
See `EXCEPTION_HANDLING_REMAINING_WORK.md` for detailed debugging steps (Call site table verification, FDE/CIE verification).

### Next Steps for Windows
1. Implement `_ThrowInfo` generation (and related `CatchableTypeArray`) so `_CxxThrowException` has type data.
2. Verify `__CxxFrameHandler3` compatibility with generated tables.

## Implementation Phases

### Phase 1: Fix Linux Exception Table Generation (HIGH PRIORITY)

**Goal**: Make exception throwing/catching work on Linux  
**Estimated Effort**: 3-5 days  
**Status**: Mostly implementation complete, debugging required.

#### 1.1 Debug Current .eh_frame Issues
- Analyze why `main()` function's FDE might be malformed or why linker complains about `.eh_frame_hdr`.
- Verify personality routine reference `__gxx_personality_v0`.

#### 1.2 Fix Landing Pad Code Generation
- Verify `__cxa_begin_catch` and `__cxa_end_catch` calls.
- Completed in `IRConverter` but needs runtime verification.

### Phase 2: RTTI Integration for Type Matching (MEDIUM PRIORITY)

**Goal**: Enable exception type matching beyond primitive types  
**Estimated Effort**: 4-6 days  
**Dependencies**: Phase 1

#### 2.1 Complete Type Info Symbol Generation
- Generate `_ZTS*` (type string) and `_ZTI*` (type info) symbols.
- Windows: `ObjFileWriter` has `mangleTypeName` but needs full `ThrowInfo` structures.

### Phase 3: Windows MSVC SEH Implementation (HIGH PRIORITY for Windows)

**Goal**: Implement Windows exception handling code generation  
**Estimated Effort**: 3-5 days (Partially Complete)  
**Status**: 🚧 Partial Implementation  

#### 3.1 .pdata and .xdata Section Generation (✅ DONE)
- `ObjFileWriter.h` implements `add_function_exception_info`, generating `UNWIND_INFO`, `FuncInfo`, `TryBlockMap`.
- Relocations for handlers added.

#### 3.2 C++ Exception Handler Data (FuncInfo) (✅ DONE)
- `FuncInfo` structure generation implemented in `ObjFileWriter.h`.
- Checks for `magicNumber`, `TryBlockMapEntry`, etc. exist.

#### 3.3 _CxxThrowException Integration (⚠️ PARTIAL)
- Call generation exists in `IRConverter.h`.
- **Missing**: `ThrowInfo` structure generation (currently passing NULL).
- **Task**: Implement `ThrowInfo`, `CatchableTypeArray`, `CatchableType` structures in `ObjFileWriter`.

### Phase 4: noexcept and Exception Specifications (LOW PRIORITY)
**Goal**: Proper noexcept handling  
**Status**: Pending.

### Phase 5: Advanced Features (FUTURE)
**Goal**: Stack Unwinding with Destructors, Rethrow, Function-Try-Blocks.

## Testing Strategy
(See existing test plans in original document)

## Files to Delete After Completion
- `docs/EXCEPTION_HANDLING_IMPLEMENTATION.md` - (Marks as superseded)
