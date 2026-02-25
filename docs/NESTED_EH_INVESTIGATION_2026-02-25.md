# Nested C++ EH Investigation — 2026-02-25

## Status: IN PROGRESS — not yet working

`test_exceptions_nested_ret0.cpp` still crashes. The original crash was STATUS_STACK_OVERFLOW
(0xC00000FD). After fixes, it now crashes with STATUS_STACK_BUFFER_OVERRUN (0xC0000409 =
__fastfail) when the inner catch rethrows.

---

## Fixes Applied (partially working)

### IRConverter.h — Stack-based try block nesting (COMPLETE, CORRECT)

Replaced the single `current_try_block_` pointer with a proper stack:

```cpp
std::vector<size_t> try_block_nesting_stack_;  // added to members
size_t pending_catch_try_index_ = SIZE_MAX;
```

- `handleTryBegin`: pushes new try index onto `try_block_nesting_stack_`
- `handleTryEnd`: pops to get correct try block index, sets `pending_catch_try_index_`
- `handleCatchBegin`: uses `pending_catch_try_index_` instead of `.back()`

Both references to `.back()` in `handleCatchBegin` were fixed (Windows path and ELF path).
Reset added to the function-reset block near line 9102.

**This fix is correct and the try/catch associations are now right.**

---

### ObjFileWriter.h — FH3 state layout (COMPLETE, MATCHES CLANG)

The state numbering now matches what clang produces:

**Sort try blocks innermost-first (by range size) BEFORE state assignment.**
**Assign states outermost-first (reverse of sorted order)** so outer try gets lower state numbers.

Result for nested try (outer contains inner):
- TryBlock[0] (inner): tryLow=1, tryHigh=1, catchHigh=2
- TryBlock[1] (outer): tryLow=0, tryHigh=2, catchHigh=3

**maxState=4, UnwindMap: 0→-1, 1→0, 2→0, 3→-1**

The unwind map chain is now generated from state layout (not just IR entries).
The IP-to-state map correctly shows state 0 (outer try) after inner try ends.

All of this **exactly matches** clang's output verified with Python binary dump.

---

## Remaining Crash: 0xC0000409

### What works
- Simple nested catch (no rethrow): `test_nested_norethrow.cpp` returns 10 ✓
- Single-level catch: all existing `test_eh_*` tests still pass ✓
- The FH3 metadata (maxState, UnwindMap, TryBlockMap, IP-to-state) matches clang ✓

### What crashes
- `throw e + 5` from inside inner catch funclet (rethrow with new value)
- Bare `throw;` from inside inner catch probably too (not tested yet)

### Analysis

The metadata is now identical to clang's. The remaining difference is in the **generated machine code** of the catch funclets.

**Clang's inner catch funclet** (from `.obj` disassembly):
```asm
mov [rsp+10h], rdx        ; save establisher frame
push rbp
sub rsp, 20h
lea rbp, [rdx+40h]        ; rbp = EstablisherFrame + frame_size (NOT parent's rbp)
mov eax, [rbp-4]          ; exception object
add eax, 5
mov [rbp-1Ch], eax
lea rcx, [rbp-1Ch]
call _CxxThrowException
int 3
```

**Our inner catch funclet**:
```asm
mov [rsp+10h], rdx
push rbp
sub rsp, 20h
lea rbp, [rdx+0B0h]       ; rbp = EstablisherFrame + 0xB0 = parent's RBP
mov eax, [rbp-5Ch]        ; exception object
...stores intermediate to multiple temps...
mov [rbp-6Ch], eax
lea rcx, [rbp-6Ch]
call _CxxThrowException
int 3
```

**Key differences:**
1. Clang's `lea rbp, [rdx+40h]` uses `frame_size` (0x40 = sub rsp operand).
   Ours uses `effective_frame_size` (0xB0 = full frame including push rbp alignment).
   Both should yield the same final rbp (= parent's RBP), so this may not be the issue.

2. **dispFrame field**: Clang emits `dispFrame=56`; we emit `dispFrame=0`.
   The significance of this difference is unclear — for non-rethrow cases dispFrame=0 works fine.

3. **Exception object slot**: our outer catch reads from `[rbp-74h]` (catchObj=60 in HType),
   clang reads from `[rbp-4]` (catchObj=56). Different offsets, but both point to where the
   CRT stores the caught exception.

4. **Extra temporaries**: our inner catch stores `e+5` through multiple temp variables
   (`[rbp-64h]`, `[rbp-6Ch]`) before passing to throw. Clang uses a single temp.
   The value passed to `_CxxThrowException` must be **still alive on the stack** when the
   exception unwinds the inner catch funclet. If our temp slots overlap with something
   the CRT needs during unwind, this could corrupt the state.

### Most Likely Root Cause

The most suspicious difference is the **multiple temp spill slots** for `e+5`. When
`_CxxThrowException` is called and the CRT begins unwinding the inner catch funclet frame,
it reads the exception object from the pointer we passed (rcx = &[rbp-6Ch]). This value is
on the stack of the inner catch funclet, which is being unwound. If the CRT reads this value
AFTER popping the funclet's stack frame, it reads garbage.

Clang passes `&[rbp-1Ch]` where `[rbp-1Ch]` is in the funclet's allocated frame (safe during
unwinding since the CRT copies the value early). Our `[rbp-6Ch]` = `[parent_rbp - 6Ch]` which
is in the PARENT'S frame — this should be safe too since parent frame isn't unwound.

Actually since both `[rbp-6Ch]` (ours) and `[rbp-1Ch]` (clang's) reference parent's frame
(same rbp in both cases), the lifetime should be fine.

**Alternative hypothesis:** `STATUS_STACK_BUFFER_OVERRUN` = __fastfail(STACK_COOKIE_CHECK_FAILURE).
This is triggered when the CRT detects stack corruption. Something in our outer catch funclet
may be writing outside its expected region. The outer catch writes `[rbp-84h]` for the return
value slot — verify this offset is within the parent's allocated frame (0xB0 bytes).

---

## Files Changed (current state in working tree)

- `src/IRConverter.h` — try block nesting stack, catch association fix
- `src/ObjFileWriter.h` — sorted state layout, unwind map chaining, IP-to-state fix

### Temporary test files to delete before committing
- `test_nested_rethrow.cpp`, `test_nested_rethrow.exe`, `test_nested_rethrow.obj`
- `test_simple_nested.cpp`, `test_simple_nested.exe`, `test_simple_nested.obj`
- `test_nested_norethrow.cpp`, `test_nested_norethrow.exe`, `test_nested_norethrow.obj`
- `test_nested_minimal.cpp`
- `test_nested_debug.obj`, `test_nested_debug.exe`
- `test_nested2.obj`, `test_nested2.exe`
- `test_nested_clang.obj`, `test_nested_clang.exe`
- `test_nested_msvc.asm`

---

## Next Investigation Steps

1. **Check if dispFrame=0 vs 56 matters** — try setting dispFrame to match clang and see
   if the crash changes. Compute it as: `effective_frame_size - sub_rsp_amount`
   (= 0xB0 - 0x20 = 0x90? or something else).

2. **Check the outer catch return value slot** — verify `[rbp-84h]` is within parent frame.
   Parent allocates 0xB0 bytes: frame runs from [rbp-0xB0] to [rbp]. 0x84 < 0xB0 so it's valid.

3. **Check multiple catch handler case** — `test_multiple_catches()` in the test file uses
   3 catch handlers for one try. The state layout for that should be straightforward (no nesting).
   Verify that the state numbering for multi-catch still works.

4. **Windows-only**: This entire investigation is Windows-specific (FH3 EH ABI). Linux ELF
   uses Itanium ABI which has separate codegen paths.

5. **Try bare rethrow** (`throw;` instead of `throw e+5`) to see if it's specifically the
   "new throw from catch" case vs "rethrow current exception".

---

## Reference: Clang's Correct Metadata (verified working)

```
maxState=4
UnwindMap: state 0→-1, 1→0, 2→0, 3→-1
TryBlockMap:
  Try[0] (inner): tryLow=1, tryHigh=1, catchHigh=2, nCatches=1
  Try[1] (outer): tryLow=0, tryHigh=2, catchHigh=3, nCatches=1
  Handler for Try[0]: adj=0, pType=_TI1H, catchObj=60, handler=catch$1, dispFrame=56
  Handler for Try[1]: adj=0, pType=_TI1H, catchObj=56, handler=catch$2, dispFrame=56
IP-to-state map:
  func+0    → -1
  func+0x21 → 1   (inner try body starts)
  func+0x31 → -1  (inner try body ends, falls to -1 in clang)
  func+0x40 → 2   (inner catch funclet)
  func+0x70 → 3   (outer catch funclet)
```

Our current output matches this except: **IP-to-state has `func+55 → 0` instead of `-1`**
for the code between the inner try end and the outer try end. This difference may or may
not affect the crash.
