# Windows SEH Full Support Implementation Plan
**Date:** 2026-02-09
**Branch:** `windows-seh-exceptions`

---

## Context

FlashCpp has partial SEH support: parsing, AST, IR, and control flow all work. But **hardware exceptions are not caught** — the program crashes with `0xC0000005` instead of entering the `__except` handler. Three concrete bugs in the `.xdata` / UNWIND_INFO generation prevent `__C_specific_handler` from dispatching exceptions correctly.

### Current State
- **Working:** `__try/__except/__finally/__leave` control flow (normal execution paths)
- **Working (single function):** Catching real hardware exceptions (access violations, divide-by-zero)
- **Broken:** Multi-function SEH (pdata/xdata relocations double-count function offset)
- **Missing:** Non-constant filter functions, `GetExceptionCode()`, `GetExceptionInformation()`, `__finally` as funclet during unwinding

### Passing Tests
- `test_seh_ret0.cpp` (8 control flow tests, return 0)
- `test_seh_finally_simple_ret52.cpp` (finally block execution, return 52)
- `test_seh_hw_simple_ret100.cpp` (null deref caught, return 100)
- `test_seh_hw_divzero_ret200.cpp` (divide-by-zero caught, return 200)

### Failing Tests
- `test_seh_hw_multi_ret44.cpp` — returns 148 instead of 44 (multi-function relocation bug)
- `test_seh_hw_twofunc_ret30.cpp` — crashes (same relocation bug)
- `test_seh_basic_ret158.cpp` — crashes (has non-constant filter + multi-function)

---

## Phase 1: Fix the Three Critical Bugs (Hardware Exceptions) — MOSTLY DONE

### Bug 1: Missing Relocations for Scope Table Entries — DONE
Added IMAGE_REL_AMD64_ADDR32NB relocations for BeginAddress, EndAddress, JumpTarget in scope table entries.

### Bug 2: Incorrect UNWIND_INFO Structure — DONE
UNWIND_INFO now dynamically built based on actual prologue (push rbp + mov rbp,rsp + sub rsp,N = 11 bytes) with correct frame register (RBP) and stack-size-dependent unwind codes.

### Bug 3: Wrong JumpTarget for __finally Entries — DONE
Changed from `jump_target = 1` to `jump_target = 0`.

### Bug 4: Last-function finalization missing SEH blocks — DONE
The second finalization point (end of codegen, line ~15787) now passes `convertSehInfoToWriterFormat()` instead of empty vector.

### Bug 5: Multi-function relocation double-counting — TODO
**Problem:** `IMAGE_REL_AMD64_ADDR32NB` computes `section_RVA + symbol_value + addend`. Pdata and scope table relocations use the function symbol (value = function_start_offset) but ALSO store function_start in the addend, causing `text_RVA + 2*function_start` for non-first functions.

**Fix:** Change relocations for pdata BeginAddress/EndAddress and scope table BeginAddress/EndAddress/JumpTarget to use the `.text` section symbol (index 0, value=0) instead of the function symbol. Then store the absolute .text offset as the addend:
- pdata BeginAddress: addend = function_start, reloc against .text symbol
- pdata EndAddress: addend = function_start + function_size, reloc against .text symbol
- scope BeginAddress: addend = function_start + try_start_offset, reloc against .text symbol
- scope EndAddress: addend = function_start + try_end_offset, reloc against .text symbol
- scope JumpTarget: addend = function_start + handler_offset, reloc against .text symbol

---

## Phase 2: Validate with MSVC Reference — DONE

### MSVC comparison findings:
- MSVC uses COMDAT sections with $LN label symbols (value=0 within COMDAT)
- MSVC generates filter funclets even for EXCEPTION_EXECUTE_HANDLER (constant 1)
- MSVC prologue: just `sub rsp, N` (no push rbp). Our prologue: `push rbp; mov rbp, rsp; sub rsp, N`
- `__C_specific_handler` does handle HandlerAddress=1 as special case for constant filter

---

## Phase 3: __finally Funclet Support

**Problem:** When an exception occurs, `__C_specific_handler` needs to *call* the __finally handler as a separate function (funclet) during stack unwinding. Currently, __finally code is inline — it works for normal control flow but NOT for exception unwinding.

### Simpler Alternative (try first):
Since the __finally code is already inline and the establisher frame (RBP) is restored by `RtlUnwindEx` before calling the handler, we might be able to make the inline code work as a funclet by:
1. Having the __finally handler start with a label that `__C_specific_handler` can jump to
2. Ending with a `ret` instruction (for the funclet path)
3. For normal flow, jumping over the `ret`

---

## Phase 4: Non-Constant Filter Functions

Filter expressions like `GetExceptionCode() == 0xC0000005 ? 1 : 0` require runtime evaluation. Currently only constant filters work.

### Implementation Steps:
1. Generate filter funclet code as a separate function in .text
2. Record filter funclet RVA in scope table's HandlerAddress field
3. Add relocation for the HandlerAddress pointing to the filter funclet

---

## Phase 5: GetExceptionCode() and GetExceptionInformation()

### GetExceptionCode()
- In filter funclet: Load from `[RCX]->ExceptionRecord->ExceptionCode`
- In __except block: Save exception code to local during filter evaluation

### GetExceptionInformation()
- Only valid in filter expressions
- In filter funclet, this is simply the RCX parameter

---

## Phase 6: Nested SEH and Edge Cases

- Multiple scope table entries, one per __try block
- Inner blocks must appear before outer blocks in scope table
- __try/__except with __try/__finally nesting

---

## Key Files Modified

| File | What Changes |
|------|-------------|
| `src/ObjFileWriter.h` | UNWIND_INFO generation, scope table with relocations, pdata fixes |
| `src/IRConverter.h` | Pass stack_frame_size, fixed last-function finalization for SEH |
| `src/TokenKind.h` | Added MSVC_Try/Except/Finally/Leave keyword IDs |
| `src/TokenTable.h` | Added __try/__except/__finally/__leave token spellings |
| `src/Parser_Expressions.cpp` | Fixed `peek().type()` → `peek().is_keyword()` |

## Existing Helper Functions

- `add_xdata_relocation(offset, symbol_name)` — `src/ObjFileWriter.h:730` — adds IMAGE_REL_AMD64_ADDR32NB relocation to .xdata section against a named symbol
- `add_pdata_relocations(pdata_offset, mangled_name, xdata_offset)` — `src/ObjFileWriter.h:689` — reference for how function symbol relocations work
- `convertSehInfoToWriterFormat()` — `src/IRConverter.h:3746` — converts internal SEH blocks to writer format
- `add_function_exception_info(...)` — `src/ObjFileWriter.h:844` — main entry point for exception info generation (now takes stack_frame_size)
