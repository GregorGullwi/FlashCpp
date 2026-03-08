# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

## System V AMD64 ABI: Two-Register Struct Passing for Non-Variadic Calls

### Status: Open (partially fixed 2026-03-06)

FlashCpp uses a pointer convention for all **non-variadic** struct arguments > 8 bytes,
which is consistent internally (caller and callee agree) but deviates from the System V
AMD64 ABI for 9–16 byte structs. External C libraries or compiler-generated code that
passes such structs in two registers (per the spec) will be incompatible.

**Variadic calls are now correct:** 9–16 byte structs passed as variadic arguments on
Linux now correctly use the two-register convention, matching what `va_arg` expects.

Implementing the full two-register callee prologue (unpack RDI + RSI into a local
stack slot) is needed for full ABI compatibility with non-variadic external code.
