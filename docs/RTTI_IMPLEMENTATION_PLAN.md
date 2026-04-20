# RTTI (Runtime Type Information) — Implementation Status

## Summary

**RTTI support now covers the implemented ELF/Linux and Windows/COFF paths below, with the remaining gaps tracked here.**

The compiler emits correct platform-specific RTTI symbols and uses standard ABI
runtimes on both platforms:

- **ELF/Linux:** `_ZTI*`/`_ZTS*` symbols, `__dynamic_cast`, `__cxa_bad_typeid`/`__cxa_bad_cast`
- **Windows/COFF:** `??_R0`–`??_R4` symbols, `__RTDynamicCast`, `__dynamic_cast_throw_bad_cast`

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

## Completed Phases (Windows/COFF)

| Phase | Feature | Location |
|-------|---------|----------|
| 1 | `??_R0` Type Descriptor emission in `.rdata` | `ObjectFileWriter::add_vtable`, `ObjectFileWriter::get_or_create_type_descriptor` |
| 2 | `??_R1` Base Class Descriptors for self and all bases | `ObjectFileWriter::add_vtable` |
| 3 | `??_R2` Base Class Array with relocations | `ObjectFileWriter::add_vtable` |
| 4 | `??_R3` Class Hierarchy Descriptor | `ObjectFileWriter::add_vtable` |
| 5 | `??_R4` Complete Object Locator at vtable[-1] | `ObjectFileWriter::add_vtable` |
| 6 | `typeid(Type)` emits LEA to `??_R0` symbol for class types | `IRConverter_ConvertMain.cpp::handleTypeid` |
| 7 | `typeid(expr)` loads Complete Object Locator from vtable[-1] | `IRConverter_ConvertMain.cpp::handleTypeid` |
| 8 | `dynamic_cast` calls MSVC `__RTDynamicCast(src_ptr, vfDelta, src_type, dst_type, isReference)` | `IRConverter_ConvertMain.cpp::handleDynamicCast` |
| 9 | Failed reference casts call `__dynamic_cast_throw_bad_cast` | `IRConverter_ConvertMain.cpp::handleDynamicCast` |
| 10 | `??_R0` Type Descriptors for built-in/arithmetic types (`H` int, `M` float, `_N` bool, `X` void, …) — no more hash placeholders; cross-TU stable typeid identity for built-ins | `ObjFileWriter_RTTI.cpp::get_or_create_builtin_type_descriptor`, `IRConverter_ConvertMain.cpp::handleTypeid` |

---

## Implementation Notes

### Windows/COFF RTTI

The Windows implementation now uses proper MSVC RTTI structures and runtime:

1. **Type Descriptors (`??_R0`)**: Created on-demand via `get_or_create_type_descriptor()`,
   which emits the 16-byte header plus mangled name in `.rdata`.

2. **`typeid` operator**:
   - `typeid(Type)` emits a LEA instruction with relocation to the `??_R0` symbol
   - `typeid(expr)` loads the vtable pointer and retrieves the Complete Object Locator from vtable[-1]
   - Built-in types emit proper `??_R0<code>@8` descriptors (e.g. `??_R0H@8` for
     `int`, `??_R0_N@8` for `bool`) using the MSVC RTTI mangling codes. The
     descriptor body stores the `.<code>` name string and relocates its vftable
     slot to `??_7type_info@@6B@`, matching MSVC-produced objects.

3. **`dynamic_cast` operator**:
   - Calls MSVC runtime `__RTDynamicCast` with proper Type Descriptor pointers
   - Handles multi-level inheritance, multiple inheritance, and cross-casts correctly
   - Returns adjusted pointer on success, nullptr on failure
   - Reference casts throw `std::bad_cast` via `__dynamic_cast_throw_bad_cast`

---

## Remaining Work

### Future Enhancements

1. ~~**`<typeinfo>` header support / vtable back-substitution**~~ **DONE** — `ItaniumManglingCtx`
   tracks substitutions and emits `S_`/`S<n>_` back-references per ABI §5.1.8.
   Multi-component non-std type names also gain the required `N...E` wrapper in
   parameter positions. See `tests/test_rtti_typeinfo_std_ret0.cpp`.

2. **vfDelta Calculation**: The `__RTDynamicCast` call currently passes 0 for the
   `vfDelta` parameter. For complex virtual inheritance scenarios, this could be
   calculated from vtable offsets for optimization.

3. **Cross-platform Testing**: While the implementation is architecturally sound,
   comprehensive testing on actual Windows systems with MSVC linker and CRT would
   validate the runtime behavior.

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
