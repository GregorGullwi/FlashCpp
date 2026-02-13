# C++ Exception Handling: Convert Catch Handlers to Proper Funclets (Windows)

## Context

The 8 C++ EH test files (`test_eh_throw_catchall_ret0.cpp`, `test_eh_twofunc_throw_ret0.cpp`, etc.) crash at runtime because catch handlers are emitted as inline code within the parent function, but Windows `__CxxFrameHandler3` expects them as separate **funclets** — mini-functions with their own prologue/epilogue that return a continuation address in RAX.

The SEH `__finally` funclet pattern (already working) provides the exact model to follow. The Linux/ELF path (Itanium ABI with `__cxa_*` functions) must remain unchanged.

## Current Status (Re-Review: 2026-02-13)

The repository already contains most of the originally planned catch-funclet slice:

- `CatchEndOp` continuation payload exists in IR (`CodeGen_Statements.cpp` + `IRTypes.h` wiring).
- Windows `handleCatchBegin()` emits catch funclet prologue (`push rbp; sub rsp, 32; mov rbp, rdx`).
- Windows `handleCatchEnd()` emits funclet epilogue and returns continuation in `RAX`.
- Catch funclet `.pdata/.xdata` generation is enabled in `ObjFileWriter` with explicit unwind codes.

So this plan is now a **phase-2 stabilization plan**, not a phase-1 implementation plan.

### Execution log (2026-02-13)

- Reproduced failures on current tree:
  - `test_eh_throw_catchall_ret0.cpp` -> runtime crash (`0x40000005` / later `0x40000409` depending on metadata tweak)
  - `test_eh_throw_catch_int_ret0.cpp` -> runtime crash (`0x40000409`)
  - `test_eh_twofunc_throw_ret0.cpp` -> runtime crash (`0x40000409`) then mismatch (`99`) after control-flow change
- Implemented return-from-catch simplification in `IRConverter.h`:
  - Replaced continuation-bridge flag/stack-slot mechanism with direct trampoline return path from catch funclet (`LEA RAX, trampoline; add rsp,32; pop rbp; ret`).
  - This reduces moving parts and aligns better with the original funclet intent.
- Added a trial metadata tweak in `ObjFileWriter.h` to avoid parent/catch RUNTIME_FUNCTION overlap by truncating parent pdata range to first catch-funclet start.
  - This changed behavior but did not fully stabilize single-function catches.
- **Added catch funclet state ranges to IP-to-state map (2026-02-13 later session)**:
  - Previously only try body states were included in the IP-to-state map
  - Now catch funclets properly register their state ranges so FH3 can track execution inside catch handlers
  - PDATA ranges remain correctly carved out to separate parent and catch funclet ranges
  - Test `test_eh_try_no_throw_ret0.cpp` (no throw, just try-catch structure) passes successfully
  - Tests with actual throws still crash with 0x40000005, indicating deeper FH3 metadata or unwinding issues remain

## Revised Next Steps (Required)

1. **Finalize correct parent/catch runtime ranges**
  - Current trial shows range shape materially affects FH behavior.
  - Implement a stable layout where continuation/trampoline IPs remain valid for FH expectations while catch funclets remain unwindable as independent ranges.

2. **Validate continuation target semantics for FH3**
  - Confirm whether continuation returned from catch funclet must be inside parent function runtime range.
  - Ensure generated continuation/trampoline labels satisfy that constraint.

3. **Re-check IP-to-state transitions against the final range model**
  - Keep catch state ranges consistent with actual emitted funclet boundaries.
  - Verify map transitions at try end, catch entry, catch exit, and post-catch continuation.

4. **Then re-run focused tests**
  - `test_eh_throw_catchall_ret0.cpp`
  - `test_eh_throw_catch_int_ret0.cpp`
  - `test_eh_twofunc_throw_ret0.cpp`
  - `test_eh_twofunc_mixed_ret0.cpp`

Only after these pass should full-suite EH validation proceed.

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

## Bugs Found and Fixed (Session 2, 2026-02-13)

**Bug 1: FuncInfo only emitted for main** (fixed, committed earlier)
- `add_function_exception_info` only wrote C++ EH FuncInfo metadata for functions whose mangled name contained "main". Changed to emit for all C++ functions with try/catch.

**Bug 2: Throw temp overlapping saved RBP** (fixed, committed earlier)
- Throw temporary stored at `[RSP+32]` overlapped the saved RBP in the stack frame. Fixed slot allocation to avoid conflict.

**Bug 3: Post-catch PDATA sharing parent's UNWIND_INFO** (fixed this session)
- Post-catch parent code ranges reused the parent's UNWIND_INFO with SizeOfProlog=11, but the post-catch code starts well past the prologue. The unwinder treated all post-catch code as mid-prologue, applying zero unwind codes.
- Fix: Create separate UNWIND_INFO with SizeOfProlog=0 for post-catch parent PDATA ranges (`ObjFileWriter.h`).

**Bug 4: Catch continuation RBP corruption** (fixed this session)
- `_JumpToContinuation` sets RSP=establisher_frame but does NOT restore RBP. Our parent epilogue `mov rsp, rbp; pop rbp; ret` used the corrupted (unreliable) RBP value.
- Fix: Emit fixup stub after funclet ret: `mov rbp, rsp; sub rsp, N; jmp continuation_label`. The catch funclet's LEA RAX returns the fixup_label instead of the continuation_label directly. The SUB RSP immediate is patched at function end with the final stack size (`IRConverter.h handleCatchEnd`).

**Bug 5: FuncInfo missing dispUnwindHelp field** (fixed this session)
- Magic `0x19930522` tells FH3 to expect 10 fields (40 bytes): `magicNumber, maxState, pUnwindMap, nTryBlocks, pTryBlockMap, nIPMapEntries, pIPtoStateMap, dispUnwindHelp, pESTypeList, EHFlags`. Our code wrote 9 fields — it jumped from `pIPtoStateMap` directly to `pESTypeList`, omitting `dispUnwindHelp`. This caused a 4-byte shift: FH3 read `pESTypeList(0)` as `dispUnwindHelp`, `EHFlags(1)` as `pESTypeList` (invalid RVA pointer!).
- Fix: Added `dispUnwindHelp = -8` field in the correct position (`ObjFileWriter.h`).

**Bug 6: No FH3 state variable** (fixed this session)
- Clang initializes a state variable at `[rbp-8]` to -2 in the prologue. FH3 with magic `0x19930522` reads this via `dispUnwindHelp`; value -2 means "use IP-to-state map for lookup".
- Fix: Pre-scan function IR for `TryBegin` opcodes; if found, reserve `[rbp-8]` by shifting parameter home space down 8 bytes, emit `mov qword [rbp-8], -2` right after the prologue sub rsp (`IRConverter.h`).

### Current status after all fixes

- ✅ `test_eh_throw_catchall_ret0.cpp` — passes (throw+catch in main)
- ✅ `test_eh_throw_catch_int_ret0.cpp` — passes (typed throw+catch in main)
- ✅ `test_eh_try_no_throw_ret0.cpp` — passes (try/catch structure, no throw)
- ✅ `temp_eh_throw_across_ret0.cpp` — passes (throw in f(), catch in main)
- ❌ `temp_eh_nonmain_empty_catch_ret0.cpp` — crashes `0x40000409` (throw+catch in non-main function)

### Analysis of remaining crash

All FuncInfo metadata now matches clang's FH3 format (magic 0x19930522, 10-field layout, state variable, proper PDATA/XDATA). The crash ONLY affects functions where throw AND catch happen in the same non-main function.

Key remaining differences vs clang output:
1. **Prologue style**: We use `push rbp; mov rbp, rsp; sub rsp, N` with `FrameOffset=0`. Clang uses `push rbp; sub rsp, N; lea rbp, [rsp+N]` with `FrameOffset=N/16`. Both compute the same establisher frame but the FrameOffset approach gives a positive `dispUnwindHelp`.
2. **dispUnwindHelp sign**: Ours is -8 (negative), clang's is +40 (positive). Mathematically equivalent, but negative may be unusual/untested in FH3.
3. **Catch funclet RDX save**: Clang emits `mov [rsp+0x10], rdx` before `push rbp` in the catch funclet. We don't. This stores the establisher frame in the caller's shadow space.
4. **IP-to-state map**: We include entries for funclet ranges and main() in f()'s map (6 entries). Clang has 4 entries (parent + funclet only).

### Next steps to try

1. Add `mov [rsp+0x10], rdx` to catch funclet prologue (matching clang)
2. Try positive `dispUnwindHelp` with `FrameOffset` approach (major prologue change)
3. Remove extraneous IP-to-state map entries (main's entry in f()'s map)
4. Link test exe and debug under WinDbg to find exact crash location in FH3

## Files Modified

1. **`src/IRTypes.h`** — `CatchEndOp` struct (done earlier)
2. **`src/CodeGen_Statements.cpp`** — Continuation label in CatchEnd (done earlier)
3. **`src/IRConverter.h`** — Funclet prologue/epilogue, catch continuation fixup stubs, FH3 state variable init, `current_function_has_cpp_eh_` flag
4. **`src/ObjFileWriter.h`** — FuncInfo magic 0x19930522 with all 10 fields including dispUnwindHelp, post-catch PDATA UNWIND_INFO with SizeOfProlog=0

## Verification

1. Build with MSBuild (Debug/x64)
2. Copy FlashCppMSVC.exe to FlashCpp.exe
3. Run specific failing tests first: `test_eh_throw_catchall_ret0.cpp`, `temp_eh_nonmain_empty_catch_ret0.cpp`
4. Run full test suite: `tests/run_all_tests.ps1`
5. Verify no new mismatches or crashes
