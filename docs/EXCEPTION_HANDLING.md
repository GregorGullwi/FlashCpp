# Exception Handling in FlashCpp

**Updated**: 2026-02-24  
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
| `noexcept` specifier | ✅ | Basic support |
| Nested try blocks | ❌ | Known crash (SIGABRT) |
| Rethrowing (`throw;`) | ❌ | Not implemented |
| Class-type exceptions with destructors | ❌ | Not implemented |
| Stack unwinding with local destructors | ❌ | Cleanup actions not emitted |

### Windows (COFF / MSVC ABI): ✅ Partial

| Feature | Status | Notes |
|---------|--------|-------|
| `.pdata` / `.xdata` generation | ✅ | UNWIND_INFO, FuncInfo layout |
| `_CxxThrowException` call generation | ✅ | ThrowInfo is NULL (type matching limited) |
| Catch funclets with establisher-frame model | ✅ | LEA RBP from RDX |
| Catch continuation and return bridging | ✅ | Fixup stubs for catch-return flow |
| `__CxxFrameHandler3` compatibility | ⚠️ | Needs ThrowInfo for full type matching |
| `__try`/`__except`/`__finally` (Win32 SEH) | ✅ | `__C_specific_handler` integration |

---

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
| `src/CodeGen_Statements.cpp` | IR generation for try/catch/throw AST nodes |
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

## References

1. [Itanium C++ ABI: Exception Handling](https://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html)
2. [LSB Exception Frames Specification](https://refspecs.linuxfoundation.org/LSB_5.0.0/LSB-Core-generic/LSB-Core-generic/ehframechpt.html)
3. [DWARF Debugging Information Format](https://dwarfstd.org/)
4. [GCC Exception Handling Internals](https://gcc.gnu.org/wiki/Internals/Exception_Handling)

## See Also

- `docs/WINDOWS_SEH_RESEARCH.md` — Research on Windows `__try`/`__except`/`__finally` (separate from C++ exceptions)
