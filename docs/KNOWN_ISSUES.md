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

## Abbreviated Function Templates: `decltype` Trailing Return Using Parameter Names

### Status: Open (verified 2026-03-07)

When an abbreviated function template uses `auto` parameters and a trailing
return type that references those parameter names via `decltype`, the compiler
fails with "Missing identifier":

```cpp
// Fails: parser does not have auto param names in scope when parsing trailing return
auto max_val(auto a, auto b) -> decltype(a > b ? a : b) { return a > b ? a : b; }

// Works: explicit template parameters are in scope for the trailing return type
template <typename A, typename B>
auto max_val(A a, B b) -> decltype(a > b ? a : b) { return a > b ? a : b; }

// Works: trailing return type that does not reference param names
auto max_val(auto a, auto b) -> int { return a > b ? a : b; }
```

The root cause is that abbreviated function templates synthesize their implicit
template parameters after the return type is parsed, so `a` and `b` are not yet
visible during trailing-return-type parsing. Workaround: use explicit `template<>`
syntax when `decltype` on parameter names is needed in the trailing return.

All other abbreviated function template forms work correctly: `auto`/`const auto&`/
`auto*`/`auto&` parameters, multiple independent `auto` parameters, constrained
`auto` (e.g., `Concept auto`), mixed explicit template params, and member functions.

## Default Argument Codegen Silently Skips Unrecognized Expression Types

### Status: Open (verified 2026-03-06)

In both `CodeGen_Call_Direct.cpp` and `CodeGen_Call_Indirect.cpp`, the default
argument fill-in code only handles `ExpressionNode` and `InitializerListNode`
default values. If a parameter's default value is stored as any other AST node
type, no argument is added to `call_op.args` and no error is reported — the
parameter is silently dropped, causing argument misalignment for subsequent
parameters.

Currently all supported default value forms (literals, identifiers, constructor
calls, braced init lists) produce one of these two node types, so this is not
triggered in practice. However, future additions (e.g., lambda defaults, fold
expressions) could hit this path silently. An `else` branch should emit an
`InternalError` or `CompileError` to catch unexpected default value types.
