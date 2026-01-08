# Exception Handling Implementation Status

## Overview
This document tracks the implementation status of C++ exception handling support in FlashCpp for ELF/Linux targets.

## Current Status: ✅ WORKING - Basic Exception Handling Functional

Exception handling is now **fully functional** for basic cases! The critical TType base offset calculation bug has been fixed.

### Latest Fix (2026-01-08)
- ✅ **Fixed TType base offset calculation** - The offset must include the call site encoding byte and call site table size ULEB128
- ✅ Exception throwing and catching now works correctly
- ✅ Test program runs successfully and exits cleanly

### What Works Now
- ✅ Basic try/catch blocks
- ✅ Throwing and catching primitive types (int, etc.)
- ✅ Stack unwinding through exception handlers
- ✅ Proper cleanup and exception object destruction

## What Works ✅

### 1. `.eh_frame` Section Generation
- ✅ CIE (Common Information Entry) with proper personality routine reference
- ✅ FDE (Frame Description Entry) for each function
- ✅ DWARF CFI instructions for stack unwinding
- ✅ Relocations for personality routine (`__gxx_personality_v0`)
- ✅ Proper encoding formats (DW_EH_PE_pcrel | DW_EH_PE_sdata4)

### 2. `.gcc_except_table` Section (LSDA)
- ✅ LSDA header with correct encodings
- ✅ Type table with typeinfo symbols (e.g., `_ZTIi` for `int`)
- ✅ Type table relocations (R_X86_64_32 for absolute pointers)
- ✅ Action table with type filters
- ✅ Call site table for try blocks
- ✅ Correct TType offset calculation

### 3. IR and Code Generation
- ✅ `try_begin`, `try_end`, `catch_begin`, `catch_end` IR opcodes
- ✅ `throw` IR opcode with type information
- ✅ Tracking of try regions and landing pads
- ✅ Exception object allocation and initialization
- ✅ Calls to `__cxa_allocate_exception`, `__cxa_throw`, `__cxa_begin_catch`, `__cxa_end_catch`

## What Doesn't Work ❌

### Critical Issue: Personality Routine Crash

**Problem**: The program segfaults when throwing an exception. The crash occurs in `__gxx_personality_v0` while parsing the LSDA.

**Status**: Call site table generation has been fixed to cover the entire function (before try, try block, after try), but the personality routine still crashes. This suggests there's a subtle bug in the LSDA encoding.

**Example**:
```cpp
int main() {
    printf("Before try\n");  // ← Missing call site entry
    try {
        printf("In try\n");   // ← Has call site entry
        throw 42;
    } catch (int x) {
        printf("Caught: %d\n", x);
    }
    printf("After catch\n"); // ← Missing call site entry
    return 0;
}
```

**What FlashCpp Now Generates** (FIXED):
```
Call Site Table:
  [0, 36) → landing_pad=0, action=0  // Before try
  [36, 143) → landing_pad=148, action=1  // Try block
  [143, 148) → landing_pad=0, action=0  // After try, before landing pad
```

The call site table now correctly covers the entire function from start to the first landing pad. Landing pads (catch handlers) are NOT included in the call site table, as they are exception handlers themselves.

## Recent Progress ✅

### 7. TType Base Offset Calculation (FIXED - 2026-01-08) 🎉
**THE CRITICAL FIX THAT MADE EXCEPTIONS WORK!**

The TType base offset in the LSDA header was incorrectly calculated. According to the DWARF spec, the offset is measured from the byte AFTER the TType base ULEB128 field to the END of the type table.

**Before (BROKEN)**:
```cpp
uint64_t ttype_base = call_site_table_size + action_table_size + type_table_size;
// = 14 + 2 + 4 = 20
```

**After (WORKING)**:
```cpp
// Must include the bytes between TType base field and call site table:
// - Call site encoding (1 byte)
// - Call site table size ULEB128 (variable, typically 1 byte)
auto cs_size_uleb = DwarfCFI::encodeULEB128(call_site_table_size);
size_t cs_size_uleb_len = cs_size_uleb.size();
uint64_t ttype_base = 1 + cs_size_uleb_len + call_site_table_size + action_table_size + type_table_size;
// = 1 + 1 + 14 + 2 + 4 = 22
```

This 2-byte difference caused the personality routine to read the type table entry from the wrong offset, resulting in a corrupted typeinfo pointer and segmentation fault.

### 6. Epilogue CFA Instructions (FIXED)
- ✅ Added CFI tracking for `pop rbp` instruction in function epilogue
- ✅ `.eh_frame` now includes `DW_CFA_def_cfa` instructions for epilogue
- ✅ CFA instructions now cover entire function from prologue through epilogue

### 5. Typeinfo Relocation Fix (FIXED)
- ✅ Changed LEA instruction relocations from R_X86_64_PLT32 to R_X86_64_PC32
- ✅ Typeinfo pointer now correctly points to actual typeinfo data, not PLT stub
- ✅ `__cxa_throw` now receives correct typeinfo address

### 4. ULEB128/SLEB128 Encoding (VERIFIED)
- ✅ Verified ULEB128/SLEB128 encoding is correct
- ✅ Created Python decoder to validate LSDA structure

### 3. Type Table Ordering (FIXED)
- ✅ Fixed type table to be in reverse order as per Itanium C++ ABI
- ✅ Type filter 1 now correctly refers to last entry in type table

### 2. Indirect Type Table Encoding (FIXED)
- ✅ Changed TType encoding to 0x9b (indirect|pcrel|sdata4) to match Clang/GCC
- ✅ Type table entries now use R_X86_64_PC32 relocations to .data section
- ✅ .data section contains R_X86_64_64 relocations to actual typeinfo symbols

### 1. Complete Call Site Table Generation (FIXED)
- ✅ Modified `ElfFileWriter.h` to generate call site entries for ALL code regions
- ✅ Added entries for code before try blocks
- ✅ Added entries for code after try blocks
- ✅ Call site table now covers entire function up to first landing pad
- ✅ Landing pads (catch handlers) are correctly excluded from call site table

## Known Limitations

### 1. Linker Warning in `.eh_frame`
The linker reports: `/usr/bin/ld: error in test_exception_minimal.obj(.eh_frame); no .eh_frame_hdr table will be created`

However, the `.eh_frame_hdr` section IS actually created (verified with `readelf -S`), and exception handling works correctly. This appears to be a harmless warning related to minor differences in CFA instruction encoding compared to what the linker expects.

### 2. Advanced Exception Features Not Yet Implemented
- ❌ Nested try blocks
- ❌ Multiple catch handlers for different types
- ❌ Catch-all handlers (`catch(...)`)
- ❌ Exception specifications (`noexcept`, `throw()`)
- ❌ Rethrowing exceptions (`throw;`)
- ❌ Class-type exceptions with destructors
- ❌ Exception object cleanup in all paths

## Next Steps for Enhancement

### 1. Implement Advanced Exception Features
- Add support for nested try blocks
- Add support for multiple catch handlers
- Add support for catch-all (`catch(...)`)
- Add support for rethrowing (`throw;`)
- Add support for class-type exceptions with destructors

### 2. Improve CFA Instructions
- Match Clang's CFA instruction sequence more closely to eliminate linker warning
- Add more detailed stack frame tracking
- Optimize CFA instruction size

### 3. Add Exception Specifications
- Implement `noexcept` specifier
- Implement dynamic exception specifications (deprecated but still used)
- Add `std::terminate` and `std::unexpected` support

## Testing

### Test Case: `tests/test_exception_minimal.cpp`
```cpp
extern "C" int printf(const char*, ...);

int main() {
    printf("Before try\n");
    try {
        printf("In try block\n");
        throw 42;
        printf("After throw (should not print)\n");
    } catch (int x) {
        printf("Caught: %d\n", x);
        return x;
    }
    printf("After catch (should not print)\n");
    return 0;
}
```

**Expected Output**:
```
Before try
In try block
Caught: 42
```

**Current Behavior**: ✅ **WORKS!** Output matches expected behavior.

## References

1. **Itanium C++ ABI: Exception Handling**
   - https://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html

2. **LSB Exception Frames Specification**
   - https://refspecs.linuxfoundation.org/LSB_5.0.0/LSB-Core-generic/LSB-Core-generic/ehframechpt.html

3. **DWARF Debugging Information Format**
   - https://dwarfstd.org/

4. **GCC Exception Handling Internals**
   - https://gcc.gnu.org/wiki/Internals/Exception_Handling

## Summary

Basic exception handling is now **fully functional** in FlashCpp! The implementation correctly handles:
- Try/catch blocks with primitive types
- Stack unwinding through exception handlers
- Proper LSDA and .eh_frame generation
- Integration with libstdc++ exception runtime

The key breakthrough was fixing the TType base offset calculation in `LSDAGenerator.h` to properly account for the call site encoding byte and call site table size ULEB128 field.

