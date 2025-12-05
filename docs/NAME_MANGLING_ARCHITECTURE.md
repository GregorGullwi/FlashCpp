# Name Mangling Architecture Decision

## Question
Should name mangling be part of the ObjectFileWriter/ElfFileWriter interface?

## Current State
- Name mangling is currently part of ObjectFileWriter interface
- Methods: `getMangledName()`, `generateMangledName()`, `addFunctionSignature()`
- IRConverter depends on these methods
- NameMangling.h contains MSVC-specific type encoding utilities

## Analysis

### Option 1: Keep Mangling in Writer Interface ✅ RECOMMENDED
**Pros:**
- Name mangling is platform-specific (MSVC vs Itanium ABI)
- Windows uses MSVC mangling, Linux uses Itanium mangling
- Mangling is tightly coupled to the object file format (COFF vs ELF)
- IRConverter remains platform-agnostic
- Clean separation of concerns: IRConverter generates IR, Writer handles platform specifics

**Cons:**
- Duplicates some logic between ObjectFileWriter and ElfFileWriter
- Writers become slightly larger

### Option 2: Separate Mangling Service
**Pros:**
- Shared mangling logic if we support cross-compilation
- Smaller writer classes
- Could be unit tested independently

**Cons:**
- Requires additional abstraction layer
- IRConverter would need to know about platform differences
- Less cohesive design (mangling separated from object file generation)
- Complicates template-based duck typing approach

### Option 3: Template Parameter for Mangler
**Pros:**
- Ultimate flexibility
- Easy to test mangling separately

**Cons:**
- Over-engineered for current needs
- More complex template instantiation
- Violates "avoid large class hierarchies" guideline

## Decision: Keep Mangling in Writer Interface

**Rationale:**
1. **Platform Coupling**: Name mangling is fundamentally platform-specific
   - Windows/COFF → MSVC mangling (?func@@YAHHH@Z)
   - Linux/ELF → Itanium ABI mangling (_Z4funcii)

2. **Encapsulation**: Each writer knows its target platform best
   - ObjectFileWriter handles MSVC conventions
   - ElfFileWriter handles GCC/Itanium conventions

3. **Simplicity**: Follows the C-style philosophy of the codebase
   - No extra abstraction layers
   - Clear ownership (writer owns mangling)
   - Template duck-typing already works

4. **Precedent**: LLVM uses a similar approach
   - Mangler is part of the target-specific backend

## Implementation Plan

### Refactor NameMangling.h into Two Files:

1. **NameMangling.h** (MSVC mangling for Windows/COFF)
   ```cpp
   namespace NameMangling::MSVC {
       std::string mangle(std::string_view name, const FunctionSignature& sig);
       std::string mangleType(const TypeSpecifierNode& type);
       // ... other MSVC-specific utilities
   }
   ```

2. **ItaniumMangling.h** (New - Itanium ABI mangling for Linux/ELF)
   ```cpp
   namespace NameMangling::Itanium {
       std::string mangle(std::string_view name, const FunctionSignature& sig);
       std::string mangleType(const TypeSpecifierNode& type);
       // ... Itanium ABI utilities
   }
   ```

3. **ObjectFileWriter** uses NameMangling::MSVC
4. **ElfFileWriter** uses NameMangling::Itanium

### Interface Consistency
Both writers maintain the same interface:
```cpp
class ObjectFileWriter {
    std::string getMangledName(std::string_view name) const;
    std::string generateMangledName(std::string_view name, const FunctionSignature& sig) const;
    std::string addFunctionSignature(...);
};

class ElfFileWriter {
    // Same interface, different implementation
    std::string getMangledName(std::string_view name) const;
    std::string generateMangledName(std::string_view name, const FunctionSignature& sig) const;
    std::string addFunctionSignature(...);
};
```

## Benefits

1. **Platform Agnostic IRConverter**: Doesn't need to know about mangling differences
2. **Type Safety**: Compile-time selection via templates
3. **Zero Runtime Overhead**: No virtual functions
4. **Easy Testing**: Can test each mangler independently
5. **Future Extensibility**: Easy to add other platforms (macOS, BSD, etc.)

## Migration Path

Phase 1 (Current PR):
- ✅ ElfFileWriter has mangling methods (currently stubbed for C linkage)
- Interface matches ObjectFileWriter

Phase 2 (Next PR):
- Create ItaniumMangling.h with Itanium ABI mangling
- Refactor NameMangling.h to NameMangling/MSVC namespace
- Update ElfFileWriter to use ItaniumMangling

Phase 3 (Future):
- Add demangling utilities if needed
- Support cross-compilation scenarios

## Conclusion

**Keep name mangling as part of the writer interface.** This is the right architectural decision because:
- Mangling is platform-specific and belongs with platform-specific code
- Maintains clean separation between IR generation and platform specifics
- Follows the simple, C-style design philosophy of the codebase
- Proven pattern used by other compilers (LLVM, GCC)
