# FlashCpp Non-Standard Behavior — Exception Handling

[← Index](../NON_STANDARD_BEHAVIOR.md)

The detailed status of every exception-handling feature is in `docs/EXCEPTION_HANDLING.md`.
For the `__cpp_exceptions` macro deviation see [08_preprocessor.md](08_preprocessor.md).

| Deviation | Platform | Status |
|-----------|----------|--------|
| Nested `try` blocks crash (SIGABRT) | Linux (Itanium EH) | ✅ Fixed |
| `throw;` (rethrow) not implemented | Linux | ✅ Fixed |
| Class-type exception object destructors not called | Both | ❌ Known |
| Stack unwinding with local destructors not implemented | Both | ❌ Known |
| Cross-function `catch` fails at runtime | Windows | ❌ Known |
| `_CxxThrowException` called with NULL `ThrowInfo` | Windows | ❌ Known |

**See:** `docs/EXCEPTION_HANDLING.md` for full details on each item.
