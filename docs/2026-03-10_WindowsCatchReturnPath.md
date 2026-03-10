# Windows catch-funclet `return` follow-up plan

This note documents the **proper long-term fix** for `return` statements inside Windows catch funclets.

## Current hotfix

The immediate regression fix is to **revert** the state change added in `src/IRConverter_Conv_VarDecl.h` that set:

- `catch_funclet_terminated_by_return_ = true;`
- `in_catch_funclet_ = false;`

directly after emitting the catch-funclet `ret` for a `return` statement inside a catch body.

That change caused `CatchEnd` handling in `src/IRConverter_Emit_EHSeh.h` to skip required Windows catch-finalization work, which led to broad runtime crashes across EH tests.

## Why the reverted approach was wrong

For Windows/MSVC-style EH, a `return` inside a catch body is **not** equivalent to “the catch is fully finalized, so skip `CatchEnd`”.

`CatchEnd` still owns important responsibilities:

- catch handler end bookkeeping
- funclet end bookkeeping
- continuation fixup emission
- bridging back into parent-function control flow

So the compiler must not use a catch-local `return` to short-circuit those steps.

## Correct semantic model

The most correct model is:

1. A `return` inside a catch records **pending parent-function return intent**.
2. Catch finalization still proceeds through the normal Windows `CatchEnd` path.
3. After catch finalization, the continuation logic decides whether to:
   - continue normal execution after the catch, or
   - return from the parent function using the saved return value.

This matches the intended source-level meaning better: the catch still exits normally as a handler, but the enclosing function returns afterward.

## Recommended design

### Keep `CatchEnd` authoritative

`src/IRConverter_Emit_EHSeh.h` should remain the single place that finalizes Windows catch handlers.

It should always handle:

- funclet epilogue ownership
- catch-handler end offsets
- catch continuation fixup stubs
- catch return-bridge setup

### Replace “terminated catch funclet” with “pending parent return”

If an extra state bit is needed, it should express something like:

- `catch_has_pending_parent_return_`

not:

- `catch_funclet_terminated_by_return_`

The first describes a post-catch control-flow decision.
The second incorrectly implies that catch finalization can be skipped.

### Preserve `in_catch_funclet_` until catch finalization is complete

The compiler may still need `in_catch_funclet_` while emitting the catch body, including register-allocation restrictions and catch-specific return lowering.

But it should only be cleared when catch finalization actually completes, not at the first emitted `ret` inside the catch body.

## Suggested implementation shape

### On `return` inside a catch funclet

- save the parent-function return value if present
- set a parent-frame flag indicating “return after catch cleanup”
- emit the catch-funclet return path needed by the ABI
- do **not** mark the catch as fully finalized

### On `CatchEnd`

- always emit Windows catch-finalization logic
- always emit or reuse the continuation fixup stub
- in the continuation/fixup path, branch based on the saved “return after catch” flag:
  - if clear: jump to the normal catch continuation label
  - if set: restore / load the saved return value and return from the parent function

## File areas likely involved

- `src/IRConverter_Conv_VarDecl.h`
  - catch-body `return` lowering
- `src/IRConverter_Emit_EHSeh.h`
  - `CatchBegin` / `CatchEnd` Windows funclet orchestration
- `src/IRConverter_Conv_Fields.h`
  - state naming / lifetime cleanup

## Validation expectations for the future implementation

At minimum, re-run:

- `test_exceptions_basic_ret0.cpp`
- `test_exceptions_catch_funclets_ret0.cpp`
- `test_eh_throw_catch_int_ret0.cpp`
- the previously reported crash list from GitHub Actions
- `./tests/run_all_tests.ps1`

## Non-goal of the hotfix

The current revert intentionally does **not** redesign catch-return lowering.

It only restores the previously working behavior and documents the correct architectural direction for a separate follow-up change.