# FlashCpp Non-Standard Behavior — Cross-Cutting: Already-Documented Issues

[← Index](../NON_STANDARD_BEHAVIOR.md)

The following deviations are fully documented in other files and are listed here for
completeness only.

| Issue | Reference |
|-------|-----------|
| Nested `try` blocks crash on Linux | `docs/EXCEPTION_HANDLING.md` |
| `throw;` rethrow not implemented on Linux | `docs/EXCEPTION_HANDLING.md` |
| Class-type exception destructors not called | `docs/EXCEPTION_HANDLING.md` |
| Stack unwinding with local destructors missing | `docs/EXCEPTION_HANDLING.md` |
| Windows cross-function `catch` runtime regression (fixed) | `docs/EXCEPTION_HANDLING.md` |
| Windows `_CxxThrowException` with NULL `ThrowInfo` | `docs/EXCEPTION_HANDLING.md` |
| `constexpr` constructor body assignments not evaluated | `docs/CONSTEXPR_LIMITATIONS.md` |
| Multi-statement `constexpr` functions not evaluated | `docs/CONSTEXPR_LIMITATIONS.md` |
| Array member access / subscript in constexpr limited | `docs/CONSTEXPR_LIMITATIONS.md` |
| `consteval` enforcement missing | `docs/MISSING_FEATURES.md` |
| 9–16 byte non-variadic structs passed by pointer (SysV) | `docs/KNOWN_ISSUES.md` |
| Default arg codegen silently drops unknown AST node types | `docs/KNOWN_ISSUES.md` |
| Assignment through reference-returning method treats return as rvalue | `docs/KNOWN_ISSUES.md` |
| Array-of-structs nested brace init not parsed | `docs/KNOWN_ISSUES.md` |
| Suboptimal IR for ref-param compound assignment | `docs/known_ir_issues.md` |
| Implicit designated init as function arg (no type name) | `docs/MISSING_FEATURES.md` |
