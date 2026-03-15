# Exception Handling in FlashCpp

**Updated**: 2026-03-15
**Platform Targets**: Linux (Itanium C++ ABI) and Windows (MSVC SEH / `__CxxFrameHandler3`)

## Current Status

### Linux (ELF / Itanium ABI): ✅ Functional

Basic and intermediate exception handling works end-to-end:

| Feature | Status | Notes |
|---------|--------|-------|
| `try`/`catch`/`throw` with primitive types | ✅ | int, double, char, etc. |
| `catch(...)` (catch-all) | ✅ | Uses NULL type table entry |
| Multiple typed catch handlers per try block | ✅ | Selector-based landing pad dispatch |
| Cross-function exception propagation | ✅ | Unwinder walks FDEs for all functions |
| `.eh_frame` (CIE/FDE with CFI) | ✅ | Generated for every function |
| `.gcc_except_table` (LSDA) | ✅ | Call sites, actions, type table |
| Personality routine (`__gxx_personality_v0`) | ✅ | Linked from libstdc++ |
| External typeinfo symbols (`_ZTIi`, `_ZTId`, …) | ✅ | Linked from libstdc++ |
| `noexcept` specifier | ✅ | Full: terminate LP emitted via `__cxa_call_terminate` |
| `noexcept(false)` specifier | ✅ | Expression evaluated at IR gen; no terminate LP added |
| `noexcept` enforcement at runtime | ✅ | Cleanup LP calls `__cxa_call_terminate` on escape |
| Nested try blocks | ✅ | Fixed: LSDA multi-handler dispatch, proper selector type entries |
| Rethrowing (`throw;`) | ✅ | Fixed: correct RSP alignment + full LSDA coverage |
| Class-type exceptions with destructors | ✅ | Fixed (Linux): proper _ZTI/_ZTS typeinfo with vtable relocs; dtor arg to __cxa_throw |
| Stack unwinding with local destructors | ✅ | Fixed (Linux): cleanup landing pads emitted for try-block-local vars (Phase 1) and function-scope vars (Phase 2) |
| Local destructors before `break`/`continue`/`return` | ✅ | Fixed: `emitDestructorsForNonLocalExit` emits scope dtors before all non-local jumps |
| **Exception hierarchy matching** | ✅ | `catch(Base&)` catches `throw Derived{}` via `__si_class_type_info` / `__vmi_class_type_info`; virtual base matching via vtable vbase offsets |
| `std::rethrow_exception` / `throw;` propagation | ✅ | `__cxa_rethrow` tested end-to-end |

### Windows (COFF / MSVC ABI): ✅ Partial

| Feature | Status | Notes |
|---------|--------|-------|
| `.pdata` / `.xdata` generation | ✅ | UNWIND_INFO, FuncInfo layout |
| `_CxxThrowException` call generation | ✅ | ThrowInfo metadata generated via `get_or_create_exception_throw_info` |
| Catch funclets with establisher-frame model | ✅ | LEA RBP from RDX |
| Catch continuation and return bridging | ✅ | Fixup stubs for catch-return flow |
| Cross-function exception propagation | ✅ | Covered by the `test_eh_twofunc_*` Windows regression tests |
| `__CxxFrameHandler3` compatibility | ⚠️ | Needs ThrowInfo for full type matching |
| `__try`/`__except`/`__finally` (Win32 SEH) | ✅ | `__C_specific_handler` integration |

---

## Windows EH Hardening Roadmap

One currently interesting hardening area is **cleanup of locals declared inside a catch body when control leaves exceptionally**.

Today, the Windows EH path already has dedicated machinery for:

- function-scope cleanup,
- try-scope cleanup before entering a catch,
- catch-funclet continuation / parent-return bridging.

The weaker spot is that catch-body locals do not yet appear to be first-class entries in the Windows unwind-state model, which makes catch-body throw/rethrow exits a likely source of missed destructor cleanup.

### Current state

The first minimal hardening step has already landed for **`throw;` / rethrow** inside a catch body:

- before lowering `Rethrow`, the frontend emits destructor calls for the active catch-body scopes.

This intentionally does **not** yet change `throw <expr>` lowering inside a catch body.

### Recommended sequencing

1. **Minimal fix first**
   Close the concrete bug with the smallest safe change. The landed minimal fix handles `throw;` / rethrow from inside a catch body by emitting cleanup for the active catch scopes before lowering `Rethrow`. This was the best first step because it fixed a real failing case with low risk.

2. **Medium refactor if we keep touching EH exits** ✅ Done
   `emitDestructorsForNonLocalExit(target_depth)` is now the shared helper in
   AST→IR used for `break`, `continue`, `return`, and `goto`. It emits destructor
   calls for all live local variables in scopes between the current execution point
   and the target scope depth before the non-local jump is taken, without modifying
   `scope_stack_` so the normal `exitScope()` / `exitFunctionScope()` paths still
   work. `goto` uses `prescanLabels()` + `label_scope_depth_map_` so that both
   forward and backward gotos know the target scope depth at jump time.

3. **Large redesign only if the area keeps surfacing bugs**
   Upgrade the Windows unwind-state / unwind-map model so catch-body locals and
   other destructible scopes become first-class unwind actions, instead of relying
   on frontend-emitted cleanup before exceptional exits. This is the highest-ceiling
   solution, but also the highest-risk and highest-effort one.

### Recommendation

Steps 1 and 2 of the hardening roadmap are complete. The remaining open item
is `__CxxFrameHandler3` full type-matching on Windows (step 3 is deferred).

### Explicit follow-ups still worth tracking

- **`throw <expr>` from inside a catch body** is now hardened on Windows: the frontend materializes the thrown value first, then cleans active catch scopes, and the Windows throw backend skips the extra copy-construction step when that payload is already materialized.
- **Nested `try/catch` inside an active catch on Windows** now has focused regression coverage for fallthrough, `return`, `throw <expr>`, and `throw;` / rethrow. The nested-return fix uses an enclosing-catch bridge plus the correct Windows EH parameter-home slot bias so `dispUnwindHelp` keeps ownership of `[rbp-8]`.

## Windows Catch-Funclet Return Model

On Windows FH3, a source-level `return` inside a catch body is **not** treated as “the catch is complete.”
Instead, the catch funclet records a pending return from the **parent** function, then the normal catch-finalization path remains authoritative.

### Why this matters

`CatchEnd` still owns several pieces of required work:

- finishing catch-handler bookkeeping,
- returning the continuation address expected by the CRT,
- creating or reusing the parent-function continuation fixup,
- deciding whether control resumes normally after the catch or returns from the parent function.

The older “terminated catch funclet” idea was wrong because it implied catch finalization could be skipped.
The landed implementation instead uses a **pending parent return** model.

### Current implementation shape

The Windows path tracks catch-return state with:

- `catch_funclet_return_slot_offset_`
- `catch_funclet_return_flag_slot_offset_`
- `catch_has_pending_parent_return_`
- `current_catch_continuation_label_`
- `catch_continuation_fixup_map_`
- `catch_return_bridges_`

The saved parent return value lives in a spill slot sized from the enclosing function's actual return convention:

- floating-point returns use the XMM0 spill path,
- integer/pointer-like returns use a sized RAX spill path,
- hidden-return / reference-return cases reserve pointer-sized storage.

The catch-return flag means:

- `0`: normal fallthrough after catch finalization,
- `1`: return from the parent function after catch finalization.

### Lowering model

When `handleReturn(...)` executes while `in_catch_funclet_` is true on Windows, it:

1. saves the pending parent return value,
2. flushes dirty registers,
3. sets `catch_has_pending_parent_return_ = true`,
4. stores `1` into the catch-return flag slot,
5. loads `RAX` with either:
   - the normal continuation label for a nested catch returning into an enclosing catch funclet, or
   - the continuation-fixup address for a top-level catch funclet returning to the parent function,
6. emits the funclet epilogue and `ret`.

`CatchEnd` remains authoritative for normal fallthrough and for top-level catch returns that need a parent-frame continuation fixup:

- it obtains the parent continuation label from `CatchEndOp`,
- creates or reuses a synthetic continuation-fixup label,
- reserves both spill slots before the first shared fixup stub,
- clears the catch-return flag on normal fallthrough,
- returns the continuation/fixup address expected by the CRT.

The continuation/fixup decides between normal fallthrough and parent return by checking the saved catch-return flag:

- if the flag is clear, execution resumes at the normal continuation label,
- if the flag is set in a nested-catch bridge, the enclosing catch funclet returns the parent-function fixup address expected by the CRT,
- if the flag is set in a top-level catch fixup, the fixup clears the flag, restores the saved parent return value, emits the parent epilogue, and returns from the enclosing function.

### Key regression coverage for this model

- `test_exceptions_catch_funclets_ret0.cpp`
- `test_eh_catch_float_return_ret0.cpp`
- `test_eh_catch_conditional_return_fallthrough_ret0.cpp`
- `test_eh_catch_shared_fixup_late_return_ret0.cpp`
- `test_eh_catch_local_dtor_fallthrough_ret0.cpp`
- `test_eh_nested_catch_return_ret0.cpp`
- `test_eh_nested_catch_fallthrough_ret0.cpp`
- `test_eh_nested_catch_throw_expr_dtor_ret0.cpp`
- `test_eh_nested_catch_rethrow_dtor_ret0.cpp`

Together these cover:

- direct returns from multiple typed catch handlers,
- floating-point return preservation,
- mixed return/fallthrough paths inside one catch,
- shared continuation-fixup reuse,
- catch-local destructor execution on normal fallthrough.

## Architecture

### Linux: Itanium C++ ABI Exception Flow

```
throw expression
  → __cxa_allocate_exception(size)
  → write value into allocated memory
  → __cxa_throw(obj_ptr, &typeinfo, destructor_ptr)
    → _Unwind_RaiseException()
      → Phase 1 (Search): personality routine walks LSDA action chains
        to find a matching catch handler (type filter > 0)
      → Phase 2 (Cleanup): unwinds frames, calls landing pads
        → Landing pad receives:
            RAX = exception object pointer
            RDX = selector (matched type_filter value)
        → Landing pad dispatches to correct catch handler via selector
          → __cxa_begin_catch(exception_ptr) → returns adjusted pointer
          → catch handler body
          → __cxa_end_catch()
```

### Multi-Handler Landing Pad Dispatch

When a try block has multiple catch handlers, a single landing pad serves all
of them.  The personality routine sets RDX to the type_filter of the matched
action entry.  The landing pad saves RAX/RDX to the stack, then compares the
selector against each handler's expected filter value:

```asm
; Landing pad entry (first handler offset)
mov [rbp-EXC_PTR], rax          ; save exception pointer
mov [rbp-SELECTOR], edx         ; save selector

; Handler 0 (catch(int))
cmp dword [rbp-SELECTOR], <filter_for_int>
jne .skip_handler_0
mov rdi, [rbp-EXC_PTR]
call __cxa_begin_catch
; ... handler body (may return) ...
call __cxa_end_catch
jmp .try_end

.skip_handler_0:
; Handler 1 (catch(double))
cmp dword [rbp-SELECTOR], <filter_for_double>
jne .skip_handler_1
mov rdi, [rbp-EXC_PTR]
call __cxa_begin_catch
; ... handler body ...

.skip_handler_1:
; Handler 2 (catch(...)) — last handler, no comparison needed
mov rdi, [rbp-EXC_PTR]
call __cxa_begin_catch
; ... handler body ...
```

Filter values are assigned at function finalization time using the same type
table ordering as the LSDA generator.  The formula is:

```
filter = type_table_size - type_index
```

where `type_index` is the 0-based position of the typeinfo symbol in the
forward-ordered type table.  Filter N refers to the entry at
`type_table_end - N * entry_size` (read in reverse).

### LSDA Structure

The Language-Specific Data Area (`.gcc_except_table`) contains:

1. **Header**: LPStart encoding, TType encoding, TType base offset,
   call site encoding
2. **Call site table**: Maps code regions to landing pads and action indices
3. **Action table**: Chains of (type_filter, next_offset) pairs
4. **Type table**: Pointers to `std::type_info` objects (read in reverse;
   NULL entry = catch-all)

### Key Historical Fixes

| Date | Fix | Impact |
|------|-----|--------|
| 2026-01-08 | TType base offset must include call-site encoding byte + ULEB128 size | **Made basic exceptions work** |
| 2026-01-08 | Typeinfo relocations: R_X86_64_PC32 not R_X86_64_PLT32 | Correct typeinfo pointers |
| 2026-02-24 | catch-all: positive type_filter → NULL type table entry (not filter=0) | **Fixed catch(...)** |
| 2026-02-24 | Filter = `size - index` (not `index + 1`) for multi-entry type tables | **Fixed multi-handler dispatch** |
| 2026-02-24 | Unified landing pad with selector dispatch + filter patching | **Enabled multiple catch handlers** |
| 2026-03-09 | `get_or_create_class_typeinfo(StructTypeInfo*)` emits `__si_class_type_info` / `__vmi_class_type_info` | **Fixed `catch(Base&)` for derived exceptions** |
| 2026-03-09 | `noexcept` enforcement: terminate LP (`__cxa_call_terminate`) injected for noexcept functions | **Enforces noexcept contract at runtime** |
| 2026-03-09 | `noexcept(false)` evaluated correctly: no terminate LP for explicitly non-noexcept functions | **Fixed regression for `noexcept(false)` functions** |
| 2026-03-09 | ELF prologue SUB RSP was capped at 240 bytes (Windows SET_FPREG limit incorrectly applied to ELF); fixed in code now living in `IRConverter_ConvertMain.h` (formerly `IRConverter_Conv_VarDecl.h` and `IRConverter_Conv_Memory.h`) | **Fixed segfault for functions with 3+ try blocks on Linux** |
| 2026-03-09 | `finalizeSections` always NOP'd `catch_continuation_sub_rsp_patches_` instead of patching with `eh_extra_stack_size`; fixed to match `handleFunctionDecl` logic (Windows-only, last function) | **Fixed potential stack corruption for Windows EH functions with >240-byte frame as last function** |
| 2026-03-09 | Virtual base typeinfo offsets: `__vmi_class_type_info` offset_flags now uses vtable-relative offset (`-(3+k)*8`) for virtual bases instead of byte offset | **Fixed `catch(VBase&)` for diamond/virtual inheritance** |
| 2026-03-09 | Vtable vbase prefix: classes with virtual bases now emit vbase offset entries before `offset_to_top` in the vtable | **Enables personality routine to locate virtual base subobjects at runtime** |
| 2026-03-09 | Diamond flag detection (`__diamond_shaped_mask` 0x2) now correctly set when two bases share a common virtual ancestor | **Correct VMI typeinfo flags for diamond inheritance** |
| 2026-03-09 | `vtable_offset` in `add_vtable` was captured before `add_typeinfo` appended to `.rodata`, causing vtable symbols/relocations to point into typeinfo data | **Fixed vtable pointer corruption for all ELF classes with RTTI** |
| 2026-03-15 | `emitDestructorsForNonLocalExit()`: emit scope destructors before `break`, `continue`, `return`, and `goto` non-local jumps; `prescanLabels()` + `label_scope_depth_map_` for forward/backward goto; combined `loop_depth_stack_` (was two separate stacks) | **Fixed missing destructor calls when exiting scopes via non-local jumps** |

---

## Compiler/Linker Flags

No special flags are needed beyond standard linking:

```bash
clang++ -no-pie -o output input.o -lstdc++ -lc
```

- **`-lstdc++`** provides `__cxa_*` functions and `__gxx_personality_v0`
- **`libgcc_s`** (implicitly linked) provides `_Unwind_*` functions
- **`--unwind-tables`** is NOT needed — FlashCpp already generates FDE entries for all functions
- **`-lunwind`** is NOT needed — `_Unwind_*` symbols come from libgcc_s

### Linker Warning

The linker may report: `error in .obj(.eh_frame); no .eh_frame_hdr table will be created`.
This is a harmless warning — the `.eh_frame_hdr` section is still created and exception
handling works correctly.

---

## Safety Notes

The implementation has been reviewed for potential infinite loops:

- ✅ All loops in LSDA/FDE generation are bounded by vector sizes
- ✅ No recursion without termination conditions
- ✅ Exception tests compile in < 12 ms

---

## Source Files

| File | Purpose |
|------|---------|
| `src/LSDAGenerator.h` | Generates `.gcc_except_table` (LSDA) — type table, action table, call sites |
| `src/DwarfCFI.h` | DWARF CFI encoding utilities (ULEB128, SLEB128, CFA instructions) |
| `src/ElfFileWriter.h` | Generates `.eh_frame` (CIE/FDE), coordinates LSDA, typeinfo symbols |
| `src/IRConverter.h` | Landing pad code generation, catch dispatch, throw codegen |
| `src/IrGenerator_Statements.cpp` | IR generation for try/catch/throw AST nodes |
| `src/IRTypes.h` | `CatchBeginOp`, `CatchEndOp`, `ThrowOp` IR instruction types |
| `src/ObjFileWriter.h` | Windows `.pdata`/`.xdata` generation, FuncInfo, catch funclets |

## Testing

Exception handling tests live in `tests/` with the naming convention
`test_eh_*_retN.cpp` or `test_exceptions_*_retN.cpp`.  They are run
automatically by `tests/run_all_tests.sh`.

Key test files:
- `test_eh_throw_catchall_ret0.cpp` — basic catch-all
- `test_eh_throw_catch_int_ret0.cpp` — typed catch with value extraction
- `test_eh_twofunc_simple_catchall_ret0.cpp` — cross-function exception
- `test_eh_twofunc_propagate_ret0.cpp` — exception propagation
- `test_exceptions_catch_funclets_ret0.cpp` — multiple typed catches + catch-all
- `test_exceptions_basic_ret0.cpp` — comprehensive exception test
- `test_eh_catch_base_ref_ret0.cpp` — `catch(Base&)` catches `throw Derived{}` via SI typeinfo
- `test_eh_catch_multi_base_ret0.cpp` — deep hierarchy: `catch(Base&)` → `catch(Middle&)` → `catch(Derived&)`
- `test_eh_catch_multiple_inheritance_ref_ret0.cpp` — multiple inheritance: `catch(Left&)`, `catch(Right&)` from `Derived : Left, Right` (3 sequential try blocks; validates ELF frame-size fix)
- `test_eh_catch_virtual_base_ref_ret0.cpp` — diamond virtual inheritance: `catch(Left&)`, `catch(VBase&)` from `throw Derived{}` where `Left, Right : virtual VBase`
- `test_eh_catch_virtual_base_value_after_ref_ret0.cpp` — by-value catch through virtual base after reference catch
- `test_eh_catch_virtual_base_value_late_handler_ret0.cpp` — later typed catch handler materializes virtual-base catch object
- `test_eh_catch_virtual_base_value_late_handler_return_ret0.cpp` — direct return from by-value virtual-base catch handler
- `test_eh_rethrow_propagate_ret0.cpp` — `throw;` (rethrow) propagates with correct type info
- `test_eh_rethrow_catch_local_dtor_ret0.cpp` — rethrow from inside a catch cleans up catch-local destructors before propagation
- `test_eh_catch_float_return_ret0.cpp` — catch-funclet parent return preserves floating-point return values
- `test_eh_catch_conditional_return_fallthrough_ret0.cpp` — catch body can return on one path and fall through on another
- `test_eh_catch_shared_fixup_late_return_ret0.cpp` — multiple handlers safely share one continuation/fixup path
- `test_eh_noexcept_normal_ret0.cpp` — noexcept functions work normally + inner try/catch
- `test_eh_break_scope_dtor_ret0.cpp` — local destructors called before `break` in loops (with and without try)
- `test_eh_continue_scope_dtor_ret0.cpp` — local destructors called before `continue` in loops (with and without try)
- `test_eh_return_scope_dtor_ret0.cpp` — local destructors called before `return` (function scope, loop scope, try scope)
- `test_eh_goto_scope_dtor_ret0.cpp` — local destructors called before `goto` (forward and backward, scope-crossing)

## References

1. [Itanium C++ ABI: Exception Handling](https://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html)
2. [LSB Exception Frames Specification](https://refspecs.linuxfoundation.org/LSB_5.0.0/LSB-Core-generic/LSB-Core-generic/ehframechpt.html)
3. [DWARF Debugging Information Format](https://dwarfstd.org/)
4. [GCC Exception Handling Internals](https://gcc.gnu.org/wiki/Internals/Exception_Handling)

## See Also

- `docs/WINDOWS_SEH_RESEARCH.md` — Research on Windows `__try`/`__except`/`__finally` (separate from C++ exceptions)
