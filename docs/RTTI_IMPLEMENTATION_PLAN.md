# RTTI (Runtime Type Information) — Remaining Work

## Summary

Phases 1–4, 6, 7, and 10 are fully implemented. The three remaining work items all stem from a single root cause: **vtable RTTI pointers are never set** (`add_vtable` emits a null slot labelled "null for now"). Fixing Phase 5 unblocks the correct behaviour of Phases 8 and 9.

### What Is Already Done

| Phase | Feature | Location |
|-------|---------|----------|
| 1 | `_ZTS*` type-string symbols in `.rodata` (WEAK) | `ElfFileWriter::get_or_create_class_typeinfo` |
| 2 | `_ZTI*` for classes with no bases, `.data.rel.ro` relocations | same |
| 3 | `__si_class_type_info` for single-inheritance | same |
| 4 | `__vmi_class_type_info` for multiple/virtual inheritance | same |
| 6 | `-lstdc++` in test scripts; runtime symbols from libstdc++ | `tests/run_all_tests.sh:239` |
| 7 | Built-in type symbols (`_ZTIi`, `_ZTIb`, …) returned as external | `ElfFileWriter::get_or_create_builtin_typeinfo` |
| 10 | `__cxa_throw` passes real `_ZTI*` via `get_or_create_class_typeinfo` | `IRConverter_ConvertMain.cpp:15099` |

---

## Remaining Work

### Phase 5: Vtable RTTI Pointer Integration

**Root cause of all remaining failures.**

`ElfFileWriter::add_vtable` emits 8 null bytes for the RTTI slot with a comment `// RTTI pointer (8 bytes, null for now)`. The `get_or_create_class_typeinfo` family of functions is fully implemented and generates correct `_ZTI*`/`_ZTS*` symbols, but is never called from `add_vtable`.

**Tasks:**

1. In `ElfFileWriter::add_vtable` (`src/ElfFileWriter_GlobalRTTI.cpp`), after the vbase-prefix and before the offset-to-top, call `get_or_create_class_typeinfo(class_name)` to obtain the `_ZTI*` symbol name and store it in `typeinfo_symbol`. The existing code block that adds the `R_X86_64_64` relocation (`if (!typeinfo_symbol.empty()) { ... }`) will then fire correctly.

2. Remove (or guard) the dead legacy path that checks `rtti_info->itanium_type_info` — `StructTypeInfo::buildRTTI()` never populates that field for ELF targets; the `itanium_type_info` pointer is always null.

3. Verify with:
   ```bash
   readelf -r output.o | grep _ZTI   # should show R_X86_64_64 reloc
   readelf -s output.o | grep _ZTI   # should show WEAK symbol
   ```

**Files:** `src/ElfFileWriter_GlobalRTTI.cpp`

---

### Phase 8: `typeid()` Operator — Correctness Fixes

Parsing and IR generation are done. Two issues remain in `IrToObjConverter::handleTypeid` (`src/IRConverter_ConvertMain.cpp:5798`):

1. **`typeid(Type)` (compile-time form)** emits `emitMovImm64(RAX, std::hash<std::string_view>{}(type_name))` — a hash, not the address of the `_ZTI*` symbol. It should instead call `writer.get_or_create_class_typeinfo(type_name)` (or `get_or_create_builtin_typeinfo` for scalar types) and emit a RIP-relative LEA relocation to the resulting symbol, exactly as `handleDynamicCast` does for the target RTTI.

2. **Null-pointer check for `typeid(expr)`** — the C++ standard requires throwing `std::bad_typeid` when `typeid` is applied to a null pointer to a polymorphic type. This check is not present. Add a null-check on the loaded object pointer before dereferencing the vtable, and call `__cxa_bad_typeid` (provided by libstdc++) on the null path.

Note: the `typeid(expr)` runtime path (loads vtable[-8]) is structurally correct; it will work automatically once Phase 5 sets the vtable RTTI slot.

**Files:** `src/IRConverter_ConvertMain.cpp`

---

### Phase 9: `dynamic_cast` — Correctness & Coverage

Parsing and IR generation are done. Two issues remain:

1. **All casts fail because source RTTI is null.** `handleDynamicCast` loads the source RTTI from `vtable[-8]` (correct), but that slot is null until Phase 5 is fixed. No code changes needed here once Phase 5 is done.

2. **`__dynamic_cast_check` is too shallow.** The auto-generated function (`emit_dynamic_cast_check_function`, `src/IRConverter_ConvertMain.cpp:16260`) only walks one level of inheritance (reads `source[16]` for the SI base pointer). This breaks:
   - Multi-level inheritance chains (e.g., `A → B → C`, casting `C*` to `A*`)
   - VMI hierarchies and cross-casts

   **Option A (recommended):** Replace the custom `__dynamic_cast_check` call with a call to the standard `__dynamic_cast` from libstdc++ (already linked), using the Itanium ABI signature:
   ```cpp
   void* __dynamic_cast(const void* src_ptr,
                        const __class_type_info* src_type,
                        const __class_type_info* dst_type,
                        ptrdiff_t src2dst_offset);  // pass -1 for unknown
   ```
   The source RTTI is already loaded from `vtable[-8]` (RDI on Linux). The target RTTI symbol (`_ZTI<target>`) is already loaded via LEA relocation. Just change the call target from `__dynamic_cast_check` to `__dynamic_cast` and adjust the argument protocol accordingly.

   **Option B:** Fix `emit_dynamic_cast_check_function` to recurse properly through VMI base arrays and handle multi-level SI chains. Higher effort, less reliable than the ABI runtime.

**Files:** `src/IRConverter_ConvertMain.cpp`

---

## ABI Compatibility Notes

- Itanium C++ ABI, 64-bit, 8-byte pointers
- Type strings: `.rodata`; type info: `.data.rel.ro`; vtables: `.rodata`
- Runtime symbols resolved from `-lstdc++` (already linked)

## References

- [Itanium C++ ABI §2.9 RTTI](https://itanium-cxx-abi.github.io/cxx-abi/abi.html)
- `docs/LINUX_ELF_SUPPORT_PLAN.md`, `docs/EXCEPTION_HANDLING_IMPLEMENTATION.md`
