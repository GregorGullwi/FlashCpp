# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

## System V AMD64 ABI: Two-Register Struct Stack Overflow

### Status: Open

When a 9–16 byte by-value struct (two-register classification) does not fit in the
remaining integer registers, the SysV AMD64 ABI requires it to be passed entirely on
the stack as two 8-byte slots (16 bytes total). Currently, the caller's first-pass
stack-overflow path in both `handleFunctionCall` and `handleConstructorCall` only
pushes one 8-byte slot via `loadTypedValueIntoRegister`, losing the upper half.

**Trigger:** A non-variadic function with enough integer parameters before a 9–16 byte
struct argument to exhaust the 6 available integer registers (e.g., 5 ints + Big3).

**Test:** `test_external_abi.cpp` Test 7 (`external_big3_after_5_ints`) exercises this
case and is expected to fail until the stack overflow path is fixed to push both halves.

**Files affected:**
- `src/IRConverter_Conv_Calls.h` — `handleFunctionCall` first-pass (lines ~192-198)
- `src/IRConverter_Conv_Calls.h` — `handleConstructorCall` first-pass (lines ~1009-1014)

