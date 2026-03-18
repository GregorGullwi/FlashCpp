# FlashCpp Non-Standard Behavior — IR Backend / Calling Convention

[← Index](../NON_STANDARD_BEHAVIOR.md)

Covers the calling-convention code now living in `src/IRConverter_ConvertMain.h` (formerly `src/IRConverter_Conv_Calls.h` and `src/IRConverter_Emit_CompareBranch.h`).
For pointer-type IR issues in the codegen layer see [06_codegen.md](06_codegen.md).

> **Legend** · ✅ Correct · ⚠️ Partial · ❌ Missing / Wrong

---

### 5.1 Non-Variadic 9–16 Byte Structs Passed by Pointer (System V AMD64) ✅

**ABI (System V AMD64 §3.2.3):** Structs whose total size is 9–16 bytes and whose fields
classify as INTEGER must be passed in two consecutive registers for non-variadic calls.

**FlashCpp:** On Linux, non-variadic 9–16 byte by-value structs are passed and received using
the System V two-register convention. Internal calls and GCC/Clang interop both use the ABI
layout for 12-byte and 16-byte INTEGER-classified structs.

**Location:** `src/IRConverter_ConvertMain.cpp`, `src/IRConverter_ConvertMain.h`
**Validation:** `tests/test_external_abi.cpp` + `tests/test_external_abi_helper.c`
