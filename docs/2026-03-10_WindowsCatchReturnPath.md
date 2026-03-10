# Windows catch-funclet return path

This note documents the Windows/MSVC-style catch-return model that is already implemented on this branch.

The important point is that a `return` inside a catch funclet is **not** treated as “the catch is done.” Instead, it records a pending return from the **parent** function, then lets normal catch finalization finish.

## Problem statement

On Windows FH3, a catch handler is emitted as a real funclet. That means a source-level `return` from inside the catch body cannot simply bypass `CatchEnd` bookkeeping.

`CatchEnd` still owns important work:

- finishing catch-handler bookkeeping,
- returning the continuation address expected by the runtime,
- emitting/reusing the parent-function continuation fixup,
- deciding whether control resumes normally after the catch or returns from the parent function.

The earlier “terminated catch funclet” idea was wrong because it implied catch finalization could be skipped. The landed implementation instead uses a **pending parent return** model.

## Current implementation shape

### State tracked in the IR converter

The current Windows path tracks these pieces of state:

- `catch_funclet_return_slot_offset_`
- `catch_funclet_return_flag_slot_offset_`
- `catch_has_pending_parent_return_`
- `current_catch_continuation_label_`
- `catch_continuation_fixup_map_`
- `catch_return_bridges_`

These are declared in `src/IRConverter_Conv_Fields.h` and reset per function in `src/IRConverter_Conv_VarDecl.h`.

### Saved return value

`src/IRConverter_Emit_EHSeh.h` provides:

- `emitSavePendingCatchParentReturnValue()`
- `emitRestorePendingCatchParentReturnValue()`

The saved value lives in a parent-frame spill slot whose size is derived from the function's actual return convention:

- floating-point returns use the XMM0 spill path,
- integer/pointer-like returns use a sized RAX spill path,
- hidden-return / reference-return cases reserve pointer-sized storage.

### Reserved slots

`src/IRConverter_Conv_VarDecl.h` provides:

- `ensureCatchFuncletReturnSlot()`
- `ensureCatchFuncletReturnFlagSlot()`

These helpers either reuse the pre-reserved catch-return area or carve slots out of the current scope when needed.

The flag slot means:

- `0`: normal fallthrough after catch finalization,
- `1`: return from the parent function after catch finalization.

## Return lowering inside a catch funclet

When `handleReturn(...)` runs while `in_catch_funclet_` is true on the Windows path, it does the following:

1. save the pending parent return value,
2. flush dirty registers,
3. set `catch_has_pending_parent_return_ = true`,
4. store `1` into the catch-return flag slot,
5. load `RAX` with the appropriate continuation-fixup address when a continuation label exists,
6. emit the funclet epilogue and `ret`.

If there is no continuation label, the path currently zeroes `RAX` before the funclet return.

The important semantic detail is that this does **not** claim catch finalization is complete. It only hands control back through the Windows funclet mechanism with enough state preserved for the parent continuation logic to finish the job.

## CatchEnd remains authoritative

The matching Windows `CatchEnd` logic lives in `src/IRConverter_Emit_EHSeh.h`.

Its current behavior is:

- obtain the parent continuation label from `CatchEndOp`,
- create or reuse a synthetic continuation-fixup label via `getOrCreateCatchContinuationFixupLabel(...)`,
- reserve both return spill slots **before** emitting the first shared fixup stub,
- clear the catch-return flag on normal fallthrough so a previous return path cannot leak into a later non-returning path,
- return the continuation/fixup address expected by the CRT.

That “reserve both slots before first stub” detail is important: multiple handlers in the same try block can share one continuation stub, so the first emitted stub must already account for the later-returning case.

## How the continuation decides between fallthrough and parent return

The continuation/fixup path checks the saved catch-return flag:

- if the flag is clear, execution resumes at the normal continuation label,
- if the flag is set, the fixup clears the flag, restores the saved parent return value, emits the parent epilogue, and returns from the enclosing function.

This logic is implemented in two places:

- the shared catch-fixup emission in `src/IRConverter_Emit_EHSeh.h`,
- the label-side bridge handling in `src/IRConverter_Conv_ControlFlow.h` via `catch_return_bridges_`.

That split allows multiple catch handlers to converge on one continuation label without losing the information that one handler requested a parent return.

## Why this model is correct

This implementation matches the source-level meaning more closely than the earlier shortcut:

- the catch still exits through normal Windows catch-finalization machinery,
- local cleanup inside the catch still happens,
- shared continuation labels still work,
- returning and non-returning catch paths can coexist safely in the same try block.

## Regression coverage to keep with this design

The most relevant tests for this path are:

- `tests/test_exceptions_catch_funclets_ret0.cpp`
- `tests/test_eh_catch_float_return_ret0.cpp`
- `tests/test_eh_catch_conditional_return_fallthrough_ret0.cpp`
- `tests/test_eh_catch_shared_fixup_late_return_ret0.cpp`
- `tests/test_eh_catch_local_dtor_fallthrough_ret0.cpp`

Together these cover:

- direct returns from multiple concrete catch handlers,
- floating-point return preservation,
- a catch body that contains a return path but falls through normally on another path,
- shared continuation-fixup reuse across multiple handlers,
- local destructor execution on normal fallthrough even when the catch also contains a return.

At the time of writing, this path and the surrounding EH fixes pass `./tests/run_all_tests.ps1` on this branch.

## Boundaries of this document

This note is only about the catch-return path.

It does **not** try to document every other EH change on the branch. The constructor-selection and indirect-storage fixes that landed alongside it are reflected in the code and regression tests.