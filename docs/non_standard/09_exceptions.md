# FlashCpp Non-Standard Behavior — Exception Handling

[← Index](../NON_STANDARD_BEHAVIOR.md)

`docs/EXCEPTION_HANDLING.md` contains the broader implementation notes, but parts of it have
drifted from the current compiler. The audit table below reflects a fresh spot-check done on
2026-03-07.

The `__cpp_exceptions` preprocessor claim from [08_preprocessor.md](08_preprocessor.md) was
re-validated during this audit and remains accurate for now: a trial enablement immediately
regressed a focused `<stdexcept>` compile. That document is left untouched here per task scope.

| Claim | Platform | Audit result |
|------|----------|--------------|
| Nested `try` blocks crash (SIGABRT) | Linux (Itanium EH) | ✅ **Stale claim.** `tests/test_exceptions_nested_ret0.cpp` now compiles, links, and returns 0 on Linux. |
| `throw;` (rethrow) not implemented | Linux | ❌ **Still broken, but wording was stale.** FlashCpp now emits `__cxa_rethrow()`, yet a focused rethrow smoke test still segfaults at runtime. |
| Class-type exception object destructors not called | Both | ❌ **Still broken.** The Linux throw path still passes a NULL destructor pointer to `__cxa_throw`, and a class-type throw/catch smoke test still crashes. |
| Stack unwinding with local destructors not implemented | Both | ❌ **Still broken.** A local-destructor unwind smoke test still returns failure on Linux, matching the missing-cleanup-action implementation gap. |
| Cross-function `catch` fails at runtime | Windows | ⚠️ **Not revalidated in this Linux session.** Keep treating this as unresolved until a fresh Windows runtime check says otherwise. |
| `_CxxThrowException` called with NULL `ThrowInfo` | Windows | ✅ **Stale claim.** Current Windows codegen builds and passes `_ThrowInfo` metadata instead of always zeroing `RDX`. |

**See also:** `docs/EXCEPTION_HANDLING.md` for the detailed design notes; when it disagrees with
the table above, prefer the freshly audited status here and treat the mismatch as a follow-up doc
sync task.
