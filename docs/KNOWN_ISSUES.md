# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

## Assignment Through Reference-Returning Methods

### Status: Open (verified 2026-03-06)

Assigning through a reference returned by a member function (e.g.,
`h.getRef() = 42;` where `getRef()` returns `int&`) does not update the
underlying member. The returned reference is treated as an rvalue rather
than an lvalue. Workaround: assign directly to the member.

## Array of Structs Aggregate Initialization

### Status: Open (verified 2026-03-06)

Initializing an array of structs with nested brace initializers
(e.g., `Pair arr[3] = {{1, 2}, {3, 4}, {5, 6}};`) fails to parse.
The parser does not handle nested initializer lists for array elements.

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
