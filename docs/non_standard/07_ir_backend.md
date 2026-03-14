# FlashCpp Non-Standard Behavior — IR Backend / Calling Convention

[← Index](../NON_STANDARD_BEHAVIOR.md)

Covers the calling-convention code now living in `src/IRConverter_ConvertMain.h` (formerly `src/IRConverter_Conv_Calls.h` and `src/IRConverter_Emit_CompareBranch.h`).
For pointer-type IR issues in the codegen layer see [06_codegen.md](06_codegen.md).

> **Legend** · ✅ Correct · ⚠️ Partial · ❌ Missing / Wrong

---

### 5.1 Non-Variadic 9–16 Byte Structs Passed by Pointer (System V AMD64) ❌ [Known]

**ABI (System V AMD64 §3.2.3):** Structs whose total size is 9–16 bytes and whose fields
classify as INTEGER must be passed in two consecutive registers for non-variadic calls.

**FlashCpp:** All non-variadic struct arguments larger than 8 bytes are passed by pointer on
Linux. Both caller and callee agree, so internal calls work; but calls to or from GCC/Clang
code using the actual ABI produce incorrect results.

**Location:** `src/IRConverter_ConvertMain.h` (code formerly in `src/IRConverter_Emit_CompareBranch.h` and `src/IRConverter_Conv_Calls.h`)
**See also:** `docs/KNOWN_ISSUES.md`
