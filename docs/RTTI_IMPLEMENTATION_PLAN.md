# RTTI (Runtime Type Information) — Implementation Status

## Summary

All ELF/Itanium RTTI phases are now implemented. The compiler emits correct
`_ZTI*`/`_ZTS*` symbols, vtable RTTI pointers, and uses the standard
`__dynamic_cast` and `__cxa_bad_typeid`/`__cxa_bad_cast` runtime on Linux.

Windows/COFF has full MSVC RTTI data structures in vtables (`??_R0`–`??_R4`)
but `typeid` and `dynamic_cast` codegen still use placeholder/internal helpers.

---

## Completed Phases (ELF/Itanium)

| Phase | Feature | Location |
|-------|---------|----------|
| 1 | `_ZTS*` type-string symbols in `.rodata` (WEAK) | `ElfFileWriter::get_or_create_class_typeinfo` |
| 2 | `_ZTI*` for classes with no bases, `.data.rel.ro` relocations | same |
| 3 | `__si_class_type_info` for single-inheritance | same |
| 4 | `__vmi_class_type_info` for multiple/virtual inheritance | same |
| 5 | Vtable RTTI pointer integration — `add_vtable` calls `get_or_create_class_typeinfo`, RTTI object in `.data.rel.ro`, vtable carries relocation | `ElfFileWriter_GlobalRTTI.cpp` |
| 6 | `-lstdc++` in test scripts; runtime symbols from libstdc++ | `tests/run_all_tests.sh:239` |
| 7 | Built-in type symbols (`_ZTIi`, `_ZTIb`, …) returned as external | `ElfFileWriter::get_or_create_builtin_typeinfo` |
| 8 | `typeid(Type)` emits LEA to `_ZTI` symbol; `typeid(expr)` has `__cxa_bad_typeid` null guard | `IRConverter_ConvertMain.cpp` |
| 9 | `dynamic_cast` calls ABI `__dynamic_cast(src, src_rtti, dst_rtti, offset_hint)`; failed reference casts call `__cxa_bad_cast` | `IRConverter_ConvertMain.cpp` |
| 10 | `__cxa_throw` passes real `_ZTI*` via `get_or_create_class_typeinfo` | `IRConverter_ConvertMain.cpp` |

---

## Remaining Work

### Windows/COFF: `typeid` Codegen

The MSVC RTTI data structures (`??_R0` Type Descriptor, `??_R1`–`??_R4`) are
already emitted by `ObjectFileWriter::add_vtable` in `src/ObjFileWriter_RTTI.cpp`.
However, `handleTypeid` on Windows still uses a **hash-based placeholder**
(`emitMovImm64(RAX, hash)`) instead of emitting a relocation to the `??_R0`
symbol.

**Tasks:**

1. Add a `get_or_create_type_descriptor(class_name)` method to `ObjectFileWriter`
   that returns the `??_R0` symbol name (creating the descriptor if needed).
2. In `handleTypeid`, when on COFF, emit `emitLeaRipRelativeWithRelocation(RAX, type_desc_symbol)`
   instead of the hash placeholder.
3. For `typeid(expr)` on polymorphic types, the MSVC ABI stores the Complete
   Object Locator pointer at `vtable[-1]`; the type descriptor is reachable
   from there. The current vtable-load path is structurally correct.

**Files:** `src/ObjFileWriter_RTTI.cpp`, `src/IRConverter_ConvertMain.cpp`

---

### Windows/COFF: `dynamic_cast` Codegen

Windows currently uses an internal `__dynamic_cast_check` helper that only
walks one level of single inheritance. This should be replaced with a call to
the MSVC runtime's `__RTDynamicCast`:

```cpp
void* __RTDynamicCast(void* src_ptr,
                      int32_t vfDelta,
                      void* src_type,      // ??_R0 type descriptor
                      void* target_type,   // ??_R0 type descriptor
                      int32_t isReference); // 1 for reference cast
```

This would handle multi-level inheritance, VMI hierarchies, and cross-casts
correctly, matching the approach taken for ELF's `__dynamic_cast`.

**Files:** `src/IRConverter_ConvertMain.cpp`

---

## ABI Compatibility Notes

- **ELF/Linux:** Itanium C++ ABI, 64-bit, 8-byte pointers. Type strings in
  `.rodata`; type info in `.data.rel.ro`; vtables in `.rodata`. Runtime symbols
  resolved from `-lstdc++` (already linked).
- **COFF/Windows:** MSVC ABI. RTTI structures (`??_R0`–`??_R4`, Complete Object
  Locator) emitted in `.rdata`. Runtime symbols resolved from MSVC CRT.

## References

- [Itanium C++ ABI §2.9 RTTI](https://itanium-cxx-abi.github.io/cxx-abi/abi.html)
- [MSVC RTTI internals](https://blog.quarkslab.com/visual-c-rtti-inspection.html)
- `docs/LINUX_ELF_SUPPORT_PLAN.md`, `docs/EXCEPTION_HANDLING_IMPLEMENTATION.md`
