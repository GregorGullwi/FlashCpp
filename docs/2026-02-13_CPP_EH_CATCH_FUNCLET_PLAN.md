# C++ Exception Handling: Convert Catch Handlers to Proper Funclets (Windows)

## Context

The 8 C++ EH test files (`test_eh_throw_catchall_ret0.cpp`, `test_eh_twofunc_throw_ret0.cpp`, etc.) crash at runtime because catch handlers are emitted as inline code within the parent function, but Windows `__CxxFrameHandler3` expects them as separate **funclets** — mini-functions with their own prologue/epilogue that return a continuation address in RAX.

The SEH `__finally` funclet pattern (already working) provides the exact model to follow. The Linux/ELF path (Itanium ABI with `__cxa_*` functions) must remain unchanged.

## Plan (Reviewed/Updated)

The direction is correct. A few sequencing details are critical:

1. `CatchEnd` must carry an explicit continuation label from IR so codegen can return a valid continuation in `RAX`.
2. Catch funclet prologue/epilogue must mirror the working SEH funclet pattern (`push rbp; sub rsp, 32; mov rbp, rdx` / `add rsp, 32; pop rbp; ret`).
3. `return` inside catch funclet needs a dedicated trampoline path (save return value to parent frame slot, return trampoline address in `RAX`).
4. Catch funclet PDATA/XDATA must be enabled only after (2), with valid unwind codes (not zeroed placeholder records).

### Step 1: Add `CatchEndOp` payload with continuation label

**File: `src/IRTypes.h`**
- Add a new `CatchEndOp` struct with a `continuation_label` field (the label the parent function jumps to after the catch handler returns)

**File: `src/CodeGen_Statements.cpp`** (`visitTryStatementNode`, line 936)
- Change CatchEnd emission (line 1056) to pass the `end_label` as continuation label in a `CatchEndOp` payload
- This tells `handleCatchEnd()` where the funclet should direct execution after catch completes

### Step 2: Emit catch funclet prologue in `handleCatchBegin()` (Windows path)

**File: `src/IRConverter.h`** (`handleCatchBegin`, line 15547)

Replace the current Windows path (`emitMovRegReg(RBP, RDX)`) with a proper funclet prologue, mirroring `handleSehFinallyBegin()`:

```cpp
// push rbp
// sub rsp, 32  (shadow space)
// mov rbp, rdx (establisher frame = parent's RBP)
emitPushReg(X64Register::RBP);
emitSubRSP(32);
emitMovRegReg(X64Register::RBP, X64Register::RDX);
```

Also set `in_catch_funclet_ = true` flag (new member) so that `handleReturn()` can detect it's inside a catch funclet.

### Step 3: Emit catch funclet epilogue in `handleCatchEnd()` (Windows path)

**File: `src/IRConverter.h`** (`handleCatchEnd`, line 15582)

Add Windows funclet epilogue that:
1. Flushes dirty registers
2. Emits `LEA RAX, [RIP + continuation_label]` using the `pending_branches_` mechanism (same RIP-relative displacement as JMP/CALL)
3. Emits funclet epilogue: `add rsp, 32; pop rbp; ret`
4. Sets `in_catch_funclet_ = false`

The LEA encoding is: `48 8D 05 <disp32>` (REX.W + LEA RAX, [RIP+disp32]). The 4-byte displacement at position +3 gets patched by `patchBranches()`.

### Step 4: Handle `return` inside catch funclet (Windows path)

**File: `src/IRConverter.h`** (`handleReturn`, line ~10470)

When `in_catch_funclet_` is true on Windows:
- Store the return value to a known parent-frame slot (return value is already in RAX or the appropriate register)
- Instead of the normal epilogue (`mov rsp, rbp; pop rbp; ret`), emit the funclet epilogue with a return trampoline:
  - `LEA RAX, [RIP + return_trampoline_label]` where the trampoline does `mov rsp, rbp; pop rbp; ret` (the parent function's epilogue)
  - `add rsp, 32; pop rbp; ret` (funclet epilogue)
- Emit the return trampoline label + parent epilogue code inline (it will be reached via the funclet's RAX return)

Use a concrete trampoline strategy:
1. Store return value to a fixed slot on the parent frame (e.g., `[rbp - 8]` or a dedicated spill slot)
2. LEA RAX to a trampoline label
3. Funclet epilogue + ret
4. At the trampoline: load from `[rbp - 8]` into EAX, then parent epilogue

Notes:
- This step should be implemented as an MVP for integer/pointer returns first (existing failing tests return integers).
- Float/aggregate return-in-catch handling can be a follow-up once baseline stability is restored.

### Step 5: Enable catch funclet PDATA/XDATA in ObjFileWriter

**File: `src/ObjFileWriter.h`** (line 1845)

- Change `if (is_cpp && false)` to `if (is_cpp)`
- Fix the UNWIND_INFO for catch funclets to include proper unwind codes:
  - `UWOP_PUSH_NONVOL` for `push rbp` (code offset, info=5 for RBP)
  - `UWOP_ALLOC_SMALL` for `sub rsp, 32` (code offset, info=(32/8)-1=3)
  - Set `FrameRegister = 0` (no frame register for funclet — funclet uses parent's RBP via RDX)
  - Count of unwind codes = 2
  - Flags = `UNW_FLAG_NHANDLER` (0, no handler for funclet itself)
  - The current UNWIND_INFO is just 4 zero bytes which is incorrect — the runtime needs valid unwind codes to properly unwind through the funclet

### Step 6: Add `in_catch_funclet_` flag and return trampoline infrastructure

**File: `src/IRConverter.h`**
- Add `bool in_catch_funclet_ = false;` member variable
- Add `uint32_t catch_funclet_return_slot_offset_ = 0;` for tracking the return value spill slot
- Reset both at function begin

### Step 7: Gate all changes behind `if constexpr` for Windows only

All new catch funclet code must be wrapped in:
```cpp
if constexpr (std::is_same_v<TWriterClass, ObjFileWriter>) { ... }
```
The ELF/Linux path remains completely unchanged — it uses the Itanium ABI model with `__cxa_begin_catch`/`__cxa_end_catch` which is fundamentally different and already working.

### Step 8: Test and verify

- Run the existing C++ EH tests that currently crash — they should now work
- Run the full test suite to verify no regressions
- Create additional test if needed for return-from-catch

Execution order for fastest validation:
1. `test_eh_throw_catchall_ret0.cpp`
2. `test_eh_twofunc_throw_ret0.cpp`
3. Remaining `test_eh_*` files
4. Full `tests/run_all_tests.ps1`

## Files to Modify

1. **`src/IRTypes.h`** — Add `CatchEndOp` struct
2. **`src/CodeGen_Statements.cpp`** — Pass continuation label in CatchEnd
3. **`src/IRConverter.h`** — Funclet prologue/epilogue in handleCatchBegin/End, return-from-catch in handleReturn, new member variables
4. **`src/ObjFileWriter.h`** — Enable + fix catch funclet PDATA/XDATA with proper UNWIND_INFO

## Verification

1. Build with MSBuild (Debug/x64)
2. Copy FlashCppMSVC.exe to FlashCpp.exe
3. Run specific failing tests first: `test_eh_throw_catchall_ret0.cpp`, `test_eh_twofunc_throw_ret0.cpp`
4. Run full test suite: `tests/run_all_tests.ps1`
5. Verify no new mismatches or crashes
