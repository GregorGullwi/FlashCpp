# RTTI (Runtime Type Information) Implementation Plan for Linux/ELF

## Executive Summary

FlashCpp currently has RTTI data structures defined but not fully implemented for ELF output. This document outlines the plan to implement complete RTTI support for Linux/Unix systems following the Itanium C++ ABI specification.

**Current Status:** RTTI structures are defined in `src/AstNodeTypes.h` but vtables contain null RTTI pointers. Type information symbols are not generated in ELF output.

**Goal:** Full RTTI support enabling:
- `typeid` operator functionality
- `dynamic_cast` for safe downcasting
- Exception handling with proper type matching
- Virtual function tables with correct RTTI pointers

## Background

### What is RTTI?

Runtime Type Information (RTTI) allows programs to query and manipulate the type of objects at runtime. In C++, RTTI provides:

1. **`typeid` operator** - Returns a `std::type_info` object describing the type
2. **`dynamic_cast` operator** - Safe downcasting with runtime type checking
3. **Exception type matching** - Allows `catch` blocks to match exception types

### Itanium C++ ABI RTTI Structure

The Itanium C++ ABI (used on Linux/Unix) defines RTTI as a hierarchy of type information structures:

```
std::type_info (base class)
  ├── __fundamental_type_info (int, bool, char, etc.)
  ├── __array_type_info
  ├── __function_type_info
  ├── __enum_type_info
  ├── __class_type_info (class with no bases)
  ├── __si_class_type_info (single inheritance)
  └── __vmi_class_type_info (multiple/virtual inheritance)
```

Each class with virtual functions gets a type_info object that:
- Contains the mangled and demangled name
- Describes inheritance relationships
- Is referenced from the vtable at offset -8 (before the first virtual function pointer)

## Current Implementation Status

### ✅ Completed Components

1. **Data Structures** (`src/AstNodeTypes.h`, lines 327-395)
   - `ItaniumBaseClassInfo` - Base class relationship descriptor
   - `ItaniumClassTypeInfo` - Class with no bases
   - `ItaniumSIClassTypeInfo` - Single inheritance
   - `ItaniumVMIClassTypeInfo` - Multiple/virtual inheritance
   - `RTTITypeInfo` - Wrapper with legacy support

2. **Symbol Name Mangling** (Partial)
   - Mangled names follow Itanium ABI rules
   - Symbol prefix: `_ZTI` (Type Information)
   - Example: `_ZTI3Dog` for class `Dog`

3. **Vtable Structure** (`src/CodeGen.h`)
   - Vtables include offset-to-top and RTTI pointer slots
   - Currently RTTI pointers are null/placeholder

### ❌ Missing Components

1. **Type Info Symbol Generation**
   - ELF symbols for `_ZTI*` (type info) not created
   - ELF symbols for `_ZTS*` (type string) not created
   - No `.rodata` or `.data.rel.ro` sections for type info data

2. **Type Info Initialization**
   - Type info structures not populated with actual data
   - Vtable pointers for type_info classes not set
   - Base class arrays not constructed

3. **Vtable RTTI Pointers**
   - Vtables don't reference the actual type info symbols
   - `-8` offset from vtable currently null or invalid

4. **Runtime Support**
   - No `__dynamic_cast` implementation (required for `dynamic_cast`)
   - No `__cxa_bad_cast` for failed dynamic casts
   - Limited integration with exception handling

## Implementation Phases

### Phase 1: Type String Generation ✨ PRIORITY

**Goal:** Generate `_ZTS*` symbols containing mangled type names.

**Tasks:**
1. Add function to `ElfFileWriter.h`: `create_type_string_symbol()`
   ```cpp
   std::string create_type_string_symbol(const std::string& class_name, 
                                          const std::string& mangled_name);
   ```

2. For each class with virtual functions:
   - Create a null-terminated string with the mangled name
   - Add to `.rodata` section with symbol `_ZTS{mangled_name}`
   - Mark as `STB_WEAK` binding (can be overridden)
   - Set visibility to `STV_DEFAULT`

3. Example output for `class Dog`:
   ```
   Symbol: _ZTS3Dog
   Section: .rodata
   Value: "3Dog\0"
   Size: 5 bytes
   Binding: WEAK
   ```

**Files to modify:**
- `src/ElfFileWriter.h` - Add type string generation
- `src/CodeGen.h` - Call during vtable finalization

**Testing:**
- Compile a simple class with virtual functions
- Verify `_ZTS*` symbol exists in `.rodata`
- Check symbol with: `readelf -s output.o | grep _ZTS`

### Phase 2: Basic Type Info Symbol Generation

**Goal:** Generate `_ZTI*` symbols for classes without inheritance.

**Tasks:**
1. Add function to `ElfFileWriter.h`: `create_class_type_info()`
   ```cpp
   std::string create_class_type_info(const StructTypeInfo* struct_info,
                                       const std::string& mangled_name);
   ```

2. For classes with no base classes:
   - Create `ItaniumClassTypeInfo` structure
   - Set vtable pointer to `_ZTVN10__cxxabiv117__class_type_infoE + 16`
   - Set name pointer to corresponding `_ZTS*` symbol
   - Add to `.data.rel.ro` section with relocations
   - Symbol: `_ZTI{mangled_name}`

3. Structure layout in memory:
   ```
   Offset | Content
   -------|--------
   0      | vtable pointer (relocation to __class_type_info vtable)
   8      | name pointer (relocation to _ZTS symbol)
   ```

**Files to modify:**
- `src/ElfFileWriter.h` - Add type info generation
- `src/CodeGen.h` - Populate type info for each class

**Testing:**
- Compile class with virtual functions but no inheritance
- Verify `_ZTI*` symbol exists with correct relocations
- Check with: `readelf -r output.o | grep _ZTI`

### Phase 3: Single Inheritance Type Info

**Goal:** Generate `_ZTI*` for classes with single public non-virtual inheritance.

**Tasks:**
1. Extend `create_class_type_info()` to handle single inheritance
2. Use `ItaniumSIClassTypeInfo` structure:
   ```
   Offset | Content
   -------|--------
   0      | vtable pointer (relocation to __si_class_type_info vtable)
   8      | name pointer (relocation to _ZTS symbol)
   16     | base class pointer (relocation to base's _ZTI symbol)
   ```

3. Detect single inheritance pattern:
   - Class has exactly one base
   - Base is public
   - Base is not virtual

**Files to modify:**
- `src/ElfFileWriter.h` - Handle SI case
- `src/CodeGen.h` - Detect inheritance pattern

**Testing:**
- Compile derived class with single base
- Verify `_ZTI*` has 3 fields (vtable, name, base)
- Check base class relocation points to base's `_ZTI`

### Phase 4: Multiple/Virtual Inheritance Type Info

**Goal:** Generate `_ZTI*` for complex inheritance hierarchies.

**Tasks:**
1. Implement `ItaniumVMIClassTypeInfo` generation
2. Structure layout:
   ```
   Offset | Content
   -------|--------
   0      | vtable pointer (relocation to __vmi_class_type_info vtable)
   8      | name pointer (relocation to _ZTS symbol)
   16     | flags (virtual inheritance, public, etc.)
   20     | num_bases
   24     | base_info[0] (8 bytes each)
   32     | base_info[1]
   ...    | ...
   ```

3. Base info encoding (per Itanium ABI 2.9.5):
   - Bits 0-7: Flags (virtual, public, offset_shift)
   - Bits 8-63: offset_flags (combined offset and flags)
   - 8-byte pointer to base class type_info

4. Handle:
   - Multiple inheritance
   - Virtual inheritance
   - Private/protected inheritance
   - Mix of above

**Files to modify:**
- `src/ElfFileWriter.h` - Full VMI implementation
- `src/CodeGen.h` - Analyze inheritance graph

**Testing:**
- Diamond inheritance pattern
- Virtual base classes
- Mix of public/private bases

### Phase 5: Vtable RTTI Pointer Integration

**Goal:** Link vtables to their corresponding type info symbols.

**Tasks:**
1. Modify vtable generation to include RTTI pointer at offset -16
2. Vtable layout:
   ```
   Offset | Content
   -------|--------
   -16    | offset-to-top (0 for primary base)
   -8     | RTTI pointer (relocation to _ZTI symbol)
   0      | vfunc[0] pointer
   8      | vfunc[1] pointer
   ...    | ...
   ```

3. Add relocation from vtable[-8] to `_ZTI{class}` symbol
4. Update vtable symbol name: `_ZTV{mangled_name}`

**Files to modify:**
- `src/CodeGen.h` - Vtable generation
- `src/ElfFileWriter.h` - Add RTTI relocation

**Testing:**
- Create class with virtual functions
- Verify vtable has correct RTTI pointer
- Test: `((void**)vptr)[-1]` points to type info

### Phase 6: Runtime Library Symbols (External Dependencies)

**Goal:** Link against runtime library providing RTTI runtime support.

**Required External Symbols:**
1. `_ZTVN10__cxxabiv117__class_type_infoE` - vtable for __class_type_info
2. `_ZTVN10__cxxabiv120__si_class_type_infoE` - vtable for __si_class_type_info
3. `_ZTVN10__cxxabiv121__vmi_class_type_infoE` - vtable for __vmi_class_type_info
4. `__dynamic_cast` - Runtime function for dynamic_cast
5. `__cxa_bad_cast` - Exception thrown on bad cast
6. `__cxa_bad_typeid` - Exception for typeid of null pointer

**Implementation Options:**

**Option A: Link with libstdc++/libc++** (Recommended for now)
- Pros: Full runtime support, well-tested
- Cons: External dependency
- Command: `clang++ -o program program.o -lstdc++`

**Option B: Minimal Custom Implementation** (Future work)
- Implement minimal `__dynamic_cast` for simple hierarchies
- Stub out exception throwing
- Pros: Self-contained
- Cons: Significant implementation effort

**Tasks:**
1. Document linking requirements
2. Add `-lstdc++` to test scripts
3. Consider option B for future enhancement

**Files to modify:**
- `tests/run_all_tests.sh` - Add `-lstdc++` to link command
- `docs/RTTI_IMPLEMENTATION_PLAN.md` - Document dependencies

### Phase 7: Fundamental Type Info

**Goal:** Generate type info for built-in types (int, bool, char, etc.)

**Tasks:**
1. Create `__fundamental_type_info` structures
2. Built-in type symbols:
   - `_ZTIi` - int
   - `_ZTIb` - bool
   - `_ZTIc` - char
   - `_ZTIv` - void
   - `_ZTIPi` - int*
   - `_ZTIPKi` - const int*
   - etc.

3. Use vtable: `_ZTVN10__cxxabiv123__fundamental_type_infoE`

**Files to modify:**
- `src/ElfFileWriter.h` - Fundamental type info generation
- `src/CodeGen.h` - Generate on-demand for used types

**Testing:**
- `typeid(int)` expressions
- Exception handling with built-in types

### Phase 8: typeid() Operator Support

**Goal:** Implement `typeid` operator code generation.

**Tasks:**
1. For `typeid(type)` - compile-time:
   - Return address of `_ZTI*` symbol
   - Cast to `const std::type_info&`

2. For `typeid(expr)` - runtime:
   - Evaluate expression
   - If polymorphic type: load vptr, get RTTI pointer at offset -8
   - If non-polymorphic: return compile-time type info
   - Check for null pointer, throw `std::bad_typeid` if null

3. Return `const std::type_info&` reference

**Files to modify:**
- `src/Parser.cpp` - Parse typeid expressions
- `src/CodeGen.h` - Generate typeid IR

**Testing:**
- `typeid(SomeClass).name()`
- `typeid(polymorphic_object)`
- Compare type_info objects with `==`

### Phase 9: dynamic_cast Support

**Goal:** Implement `dynamic_cast` operator code generation.

**Tasks:**
1. Parse `dynamic_cast<Target*>(source)` expressions
2. Code generation:
   ```cpp
   void* __dynamic_cast(
       const void* src_ptr,
       const __class_type_info* src_type,
       const __class_type_info* dst_type,
       ptrdiff_t src2dst_offset
   );
   ```

3. For `dynamic_cast<Derived*>(base_ptr)`:
   - Get source vptr
   - Load source RTTI from vptr[-1]
   - Call `__dynamic_cast(base_ptr, src_rtti, &_ZTIDerived, -1)`
   - Return result (nullptr if cast fails)

4. For references, throw `std::bad_cast` on failure

**Files to modify:**
- `src/Parser.cpp` - Parse dynamic_cast
- `src/CodeGen.h` - Generate runtime call

**Testing:**
- Downcast in single inheritance
- Downcast in multiple inheritance
- Cross-cast in diamond inheritance
- Failed cast returns nullptr

### Phase 10: Exception Type Matching Integration

**Goal:** Use RTTI for exception handler matching.

**Tasks:**
1. Update `__cxa_throw` calls to pass actual type_info:
   ```cpp
   void __cxa_throw(
       void* thrown_exception,
       std::type_info* tinfo,
       void (*destructor)(void*)
   );
   ```

2. Previously passed NULL, now pass `&_ZTI{ExceptionType}`

3. Exception handler matching uses RTTI:
   - Compare thrown type with catch clause type
   - Check inheritance relationships
   - Handle catch(...) and polymorphic exceptions

**Files to modify:**
- `src/CodeGen.h` - Update throw statement codegen
- `src/ElfFileWriter.h` - Ensure exception type info available

**Testing:**
- Throw derived, catch base
- Multiple catch clauses
- Polymorphic exception hierarchies

## Implementation Priority

### High Priority (Enables basic RTTI)
1. Phase 1: Type strings
2. Phase 2: Basic type info
3. Phase 5: Vtable integration
4. Phase 6: External runtime linking

### Medium Priority (Enables operators)
5. Phase 3: Single inheritance
6. Phase 8: typeid() operator
7. Phase 9: dynamic_cast operator

### Lower Priority (Advanced features)
8. Phase 4: Multiple/virtual inheritance
9. Phase 7: Fundamental types
10. Phase 10: Exception integration

## Testing Strategy

### Unit Tests

Create test files in `tests/` directory:

1. `test_rtti_basic.cpp` - Simple class with typeid
2. `test_rtti_si.cpp` - Single inheritance hierarchy
3. `test_rtti_mi.cpp` - Multiple inheritance
4. `test_rtti_dynamic_cast.cpp` - dynamic_cast tests
5. `test_rtti_exceptions.cpp` - Exception type matching

### Integration Tests

1. Verify symbol generation:
   ```bash
   readelf -s output.o | grep _ZTI
   readelf -s output.o | grep _ZTS
   ```

2. Check relocations:
   ```bash
   readelf -r output.o | grep -A2 _ZTI
   ```

3. Runtime behavior:
   ```bash
   ./test_program
   echo $?  # Should match expected exit code
   ```

### Regression Tests

- Ensure non-RTTI code still works
- Verify vtable layout unchanged for non-polymorphic classes
- Check that compile times don't significantly increase

## Known Issues and Limitations

### Current Bugs to Fix

1. **Reference Binding Constructor Call** (`test_covariant_return.cpp`, line 126)
   - Bug: `Base& base_ref = d;` generates call to `Base::Base(Derived)`
   - Expected: Direct reference binding, no constructor call
   - Impact: Linker error for non-existent copy constructor
   - Fix: Update reference initialization codegen in `visitVariableDeclarationNode`

2. **Null RTTI Pointers in Vtables**
   - Bug: Vtables have null at offset -8 instead of type info pointer
   - Impact: Runtime RTTI operations will crash
   - Fix: Phase 5 implementation

### ABI Compatibility Notes

1. **Itanium C++ ABI Version**: Target version 1.86 (current as of 2024)
2. **Pointer Size**: 64-bit (8 bytes)
3. **Alignment**: Type info structures aligned to 8 bytes
4. **Section Placement**:
   - Type strings: `.rodata` (read-only)
   - Type info: `.data.rel.ro` (read-only after relocation)
   - Vtables: `.rodata` (with relocations)

### Unsupported Features (Out of Scope)

- `type_info::before()` comparison
- `type_info::hash_code()`
- Extended type info for template types
- Pointer-to-member type info

## Performance Considerations

### Binary Size Impact

Each polymorphic class adds:
- Type string: ~20-50 bytes (class name length)
- Type info: 16-40 bytes (depending on inheritance)
- Vtable overhead: +8 bytes for RTTI pointer

Estimate: ~50-100 bytes per polymorphic class

### Runtime Performance

- `typeid()` on polymorphic type: 2 memory loads (vptr, rtti)
- `dynamic_cast`: Function call + inheritance graph traversal
- Negligible impact on non-RTTI code

### Optimization Opportunities

1. **Lazy Type Info Generation**: Only generate for classes actually used with RTTI
2. **Type Info Deduplication**: Weak symbols allow linker to merge duplicates
3. **Inline typeid()**: For non-polymorphic types, return constant address

## References

### Specifications

1. [Itanium C++ ABI](https://itanium-cxx-abi.github.io/cxx-abi/abi.html)
   - Section 2.9: Run-Time Type Information (RTTI)
   - Section 2.9.5: Base Class Information

2. [Itanium C++ ABI Exception Handling](https://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html)
   - Type matching algorithm

### Implementation Examples

1. **LLVM libcxxabi**: `src/private_typeinfo.cpp`
   - Reference implementation of `__dynamic_cast`
   - Shows type_info vtables

2. **GCC libsupc++**: `libsupc++/tinfo.cc`
   - Alternative implementation
   - Handles edge cases

3. **ELF Specification**: Symbol versioning and weak symbols

### Related FlashCpp Documents

- `docs/LINUX_ELF_SUPPORT_PLAN.md` - Overall ELF implementation status
- `docs/EXCEPTION_HANDLING_IMPLEMENTATION.md` - Exception handling with RTTI
- `docs/NAME_MANGLING_ARCHITECTURE.md` - Symbol name mangling rules

## Appendix A: Symbol Naming Examples

| Class | Mangled Name | Type String Symbol | Type Info Symbol |
|-------|--------------|-------------------|------------------|
| `Dog` | `3Dog` | `_ZTS3Dog` | `_ZTI3Dog` |
| `Animal` | `6Animal` | `_ZTS6Animal` | `_ZTI6Animal` |
| `std::string` | `NSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE` | `_ZTSNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE` | `_ZTINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE` |

## Appendix B: Example Type Info Structure

For class `Derived : public Base`:

```cpp
// _ZTS7Derived (type string in .rodata)
const char _ZTS7Derived[] = "7Derived";

// _ZTI7Derived (type info in .data.rel.ro)
struct __si_class_type_info _ZTI7Derived = {
    .vtable = &_ZTVN10__cxxabiv120__si_class_type_infoE + 16,  // Relocation
    .name = _ZTS7Derived,                                       // Relocation
    .base_type = &_ZTI4Base                                     // Relocation
};
```

In ELF format:
```
Section: .data.rel.ro
Symbol: _ZTI7Derived
Size: 24 bytes

Offset | Value | Relocation
-------|-------|------------
0      | 0     | R_X86_64_64 -> _ZTVN10__cxxabiv120__si_class_type_infoE + 16
8      | 0     | R_X86_64_64 -> _ZTS7Derived
16     | 0     | R_X86_64_64 -> _ZTI4Base
```

## Appendix C: Implementation Checklist

- [ ] Phase 1: Type String Generation
  - [ ] Implement `create_type_string_symbol()`
  - [ ] Add to `.rodata` section
  - [ ] Set WEAK binding
  - [ ] Test with readelf

- [ ] Phase 2: Basic Type Info
  - [ ] Implement `create_class_type_info()`
  - [ ] Add to `.data.rel.ro` section
  - [ ] Generate relocations
  - [ ] Test with simple class

- [ ] Phase 3: Single Inheritance
  - [ ] Detect SI pattern
  - [ ] Use `__si_class_type_info`
  - [ ] Add base pointer relocation
  - [ ] Test with derived class

- [ ] Phase 4: Multiple/Virtual Inheritance
  - [ ] Implement VMI detection
  - [ ] Generate base info array
  - [ ] Handle virtual bases
  - [ ] Test diamond pattern

- [ ] Phase 5: Vtable Integration
  - [ ] Add RTTI pointer at vtable[-8]
  - [ ] Generate relocation
  - [ ] Update tests

- [ ] Phase 6: Runtime Linking
  - [ ] Document -lstdc++ requirement
  - [ ] Update test scripts
  - [ ] Verify __dynamic_cast available

- [ ] Phase 7: Fundamental Types
  - [ ] Built-in type symbols
  - [ ] Pointer type info
  - [ ] Const/volatile variants

- [ ] Phase 8: typeid() Operator
  - [ ] Parse typeid expressions
  - [ ] Generate compile-time typeid
  - [ ] Generate runtime typeid
  - [ ] Handle null pointer check

- [ ] Phase 9: dynamic_cast
  - [ ] Parse dynamic_cast
  - [ ] Generate __dynamic_cast call
  - [ ] Handle nullptr return
  - [ ] Handle bad_cast for references

- [ ] Phase 10: Exception Integration
  - [ ] Pass real type_info to __cxa_throw
  - [ ] Update exception tests
  - [ ] Test type matching

## Conclusion

This implementation plan provides a structured approach to adding full RTTI support to FlashCpp for Linux/ELF targets. The phased approach allows for incremental development and testing, with each phase building on the previous one.

The plan prioritizes the most impactful features first (basic type info and vtable integration) while leaving advanced features for later phases. This ensures that common use cases work early in the implementation process.

Once completed, FlashCpp will have full RTTI support compatible with the Itanium C++ ABI, enabling `typeid`, `dynamic_cast`, and proper exception type matching on Linux systems.
