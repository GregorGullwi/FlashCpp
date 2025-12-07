# Linux Exception Handling Stubs

This file provides minimal stub implementations of Itanium C++ ABI exception handling functions. These stubs allow FlashCpp-compiled code to link and run for basic testing, even though full exception handling (with .eh_frame and .gcc_except_table) is not yet implemented.

## Purpose

- **Testing**: Verify that exception throwing code is correctly generated
- **Debugging**: See diagnostic messages showing exception flow
- **Development**: Work on exception-related features without full unwinding support

## What Works

- ✅ Exception allocation (`__cxa_allocate_exception`)
- ✅ Exception throwing starts (`__cxa_throw`)
- ✅ Type_info is passed correctly
- ✅ Diagnostic messages explain what's missing

## What Doesn't Work

- ❌ Stack unwinding (requires .eh_frame section)
- ❌ Finding catch handlers (requires .gcc_except_table section)
- ❌ Landing pad execution (no personality routine integration)
- ❌ Actual exception catching

## Usage

Compile and link with your FlashCpp-generated object file:

```bash
# Compile your test
./x64/Debug/FlashCpp tests/test_exceptions_basic.cpp

# Compile the stubs
g++ -c tests/linux_exception_stubs.cpp -o linux_exception_stubs.o

# Link together
gcc -o test_program test_exceptions_basic.obj linux_exception_stubs.o

# Run (will abort with diagnostic message)
./test_program
```

## Output Example

```
STUB: __cxa_allocate_exception(8) -> 0x55d5f9f2e2a0
STUB: __cxa_throw(0x55d5f9f2e2a0, 0x55d5f1a20, (nil))
STUB: Exception thrown but no exception tables present!
STUB: Cannot find catch handlers without .eh_frame and .gcc_except_table
STUB: Calling std::terminate()
Aborted (core dumped)
```

## For Production

For production code, link with the full C++ runtime library instead:

```bash
gcc -o test_program test_exceptions_basic.obj -lstdc++
```

However, this will also fail at runtime without exception tables, but with less informative error messages.

## Next Steps for Full Exception Support

1. Implement .eh_frame generation (DWARF CFI)
2. Implement .gcc_except_table generation (LSDA)
3. Add personality routine reference
4. Generate proper landing pad code

These are substantial compiler features requiring ~2000+ lines of specialized code.
