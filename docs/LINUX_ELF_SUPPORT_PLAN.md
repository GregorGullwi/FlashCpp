# Linux ELF Support Implementation Plan

## Overview
This document outlines the plan to extend FlashCpp compiler to support Linux ELF (Executable and Linkable Format) object file generation alongside the existing Windows COFF support. The goal is to enable linking and execution on Linux while maintaining the existing Windows functionality.

**Note on Scope**: This initial implementation focuses on linking capability on Linux, NOT full ABI compatibility. Function calling conventions and other ABI aspects will remain as-is for now.

## Background

### Current Architecture
- FlashCpp currently generates **COFF (Common Object File Format)** object files for Windows
- Uses the **COFFI** library (located in `src/coffi/`) for COFF generation
- Core object file writing is in `src/ObjFileWriter.h` (class `ObjectFileWriter`)
- The `ObjectFileWriter` class is templated and used by `IRConverter` to generate machine code
- Debug information uses **CodeView** format (Windows-specific)

### Target Architecture
- Add support for **ELF (Executable and Linkable Format)** for Linux
- Use **ELFIO** library (similar to COFFI but for ELF format)
- Keep existing COFF support fully functional
- Runtime selection based on target platform

## Implementation Approach

### Philosophy: C-Style with C++ Features
Following the repository's style guide:
- **No large class hierarchies** - use simple structs and functions
- **Minimal inheritance** - prefer composition over inheritance
- **C++ features allowed**: templates, RAII, std containers, string_view, etc.
- **Think more C than C++** for the core logic

### Design Pattern: Parallel Implementation
Instead of creating an abstract base class hierarchy, we'll:
1. Create a separate `ElfFileWriter` class parallel to `ObjectFileWriter`
2. Both classes will have the same interface (duck typing via templates)
3. Use compile-time selection (templates) or runtime factory pattern for choosing writer
4. Keep the interface minimal and focused

## Detailed Implementation Steps

### Phase 1: Setup and Infrastructure (Initial)

#### 1.1 Add ELFIO Library
- **Location**: `src/elfio/` (parallel to `src/coffi/`)
- **Source**: ELFIO is a header-only library (https://github.com/serge1/ELFIO)
- **License**: MIT (compatible with our project)
- **Action**: Download ELFIO headers and place in `src/elfio/`

#### 1.2 Platform Detection
- **File**: `src/CompileContext.h` (already has target architecture enum)
- **Enhancement**: Add runtime platform detection or command-line option
- **Default behavior**: 
  - On Windows: generate COFF
  - On Linux: generate ELF
  - Optional: `--output-format=coff|elf` flag

### Phase 2: ELF File Writer Implementation

#### 2.1 Create ElfFileWriter Class
- **File**: `src/ElfFileWriter.h` (new file, parallel to `ObjFileWriter.h`)
- **Interface**: Match `ObjectFileWriter` interface for template compatibility
- **Key methods to implement**:
  ```cpp
  class ElfFileWriter {
  public:
      ElfFileWriter();  // Initialize ELF file structure
      
      // Core methods (matching ObjectFileWriter interface)
      void write(const std::string& filename);
      void add_function_symbol(std::string_view name, uint32_t offset, uint32_t stack_space, Linkage linkage);
      void add_data(const std::vector<char>& data, SectionType section_type);
      void add_relocation(uint64_t offset, std::string_view symbol_name);
      void add_relocation(uint64_t offset, std::string_view symbol_name, uint32_t relocation_type);
      
      // String literals and globals
      std::string add_string_literal(const std::string& str_content);
      void add_global_variable_data(std::string_view var_name, size_t size_in_bytes, 
                                    bool is_initialized, const std::vector<char>& init_data);
      
      // RTTI/Vtables (if needed for C++ objects)
      void add_vtable(const std::string& vtable_symbol, 
                     const std::vector<std::string>& function_symbols,
                     const std::string& class_name,
                     const std::vector<std::string>& base_class_names,
                     const std::vector<BaseClassDescriptorInfo>& base_class_info);
      
      // Function signatures and mangling (for C++ name mangling)
      std::string getMangledName(std::string_view name) const;
      std::string generateMangledName(std::string_view name, const FunctionSignature& sig) const;
      std::string addFunctionSignature(/* ... */);
      
      // Debug info (DWARF instead of CodeView)
      void add_source_file(const std::string& filename);
      void finalize_debug_info();
      // ... other debug methods
  };
  ```

#### 2.2 ELF Sections Mapping
Map existing COFF sections to ELF equivalents:

| COFF Section | ELF Section | Purpose |
|--------------|-------------|---------|
| `.text` | `.text` | Executable code |
| `.data` | `.data` | Initialized data |
| `.bss` | `.bss` | Uninitialized data |
| `.rdata` | `.rodata` | Read-only data (constants, strings) |
| `.xdata` | N/A | Exception handling data (Windows-specific) |
| `.pdata` | N/A | Procedure data (Windows-specific) |
| `.debug$S` | `.debug_*` | Debug symbols (DWARF format) |
| `.debug$T` | `.debug_*` | Debug type info (DWARF format) |

**Notes**:
- ELF doesn't use `.xdata`/`.pdata` - exception handling is different
- For initial implementation, we can skip exception handling support on Linux
- Debug info will need significant changes (CodeView → DWARF)

#### 2.3 Symbol Table
- **ELF symbol types**:
  - `STT_NOTYPE`: No type (default)
  - `STT_OBJECT`: Data object (variables)
  - `STT_FUNC`: Function
  - `STT_SECTION`: Section symbol
  - `STT_FILE`: Source file name
  
- **Symbol binding**:
  - `STB_LOCAL`: Local symbols (file scope)
  - `STB_GLOBAL`: Global symbols (external linkage)
  - `STB_WEAK`: Weak symbols

#### 2.4 Relocations
Map COFF relocation types to ELF for x86-64:

| COFF Relocation | ELF Relocation | Description |
|-----------------|----------------|-------------|
| `IMAGE_REL_AMD64_REL32` | `R_X86_64_PC32` | 32-bit PC-relative |
| `IMAGE_REL_AMD64_ADDR64` | `R_X86_64_64` | 64-bit absolute address |
| `IMAGE_REL_AMD64_ADDR32NB` | `R_X86_64_32` | 32-bit absolute address |

### Phase 3: Name Mangling

#### 3.1 C++ Name Mangling
- **Current**: MSVC-style mangling (e.g., `?func@@YAHHH@Z`)
- **Linux**: Itanium ABI mangling (e.g., `_Z4funcii`)
- **Architecture Decision**: Keep mangling in writer interface (see [NAME_MANGLING_ARCHITECTURE.md](NAME_MANGLING_ARCHITECTURE.md))
- **Strategy**: 
  - Refactor `NameMangling.h` into `NameMangling::MSVC` namespace
  - Create new `ItaniumMangling.h` with `NameMangling::Itanium` namespace
  - ObjectFileWriter uses MSVC mangling
  - ElfFileWriter uses Itanium mangling
  - IRConverter remains platform-agnostic

**Decision for MVP**: Use C linkage (unmangled names) for initial implementation. Full Itanium mangling will be added in a follow-up phase.

### Phase 4: Debug Information (DWARF)

#### 4.1 DWARF vs CodeView
- **Current**: CodeView debug format (Windows)
- **Linux**: DWARF debug format
- **Complexity**: DWARF is significantly different from CodeView
- **Strategy for MVP**: 
  - Implement minimal DWARF support (basic line number info)
  - Full DWARF implementation deferred to later phase
  - Can link without debug info initially

#### 4.2 Minimal DWARF Implementation
- `.debug_line`: Line number information
- `.debug_info`: Minimal compilation unit info
- `.debug_abbrev`: Abbreviations table
- Defer: `.debug_frame` (call frame info), full type info, etc.

### Phase 5: Integration and Testing

#### 5.1 Modify IRConverter
- **File**: `src/IRConverter.h`
- **Current**: `template<class TWriterClass = ObjectFileWriter>`
- **Change**: Support both `ObjectFileWriter` and `ElfFileWriter`
- **Method**: Template already supports this! Just need to instantiate with correct type

#### 5.2 Main Entry Point
- **File**: `src/main.cpp`
- **Changes**:
  ```cpp
  // Detect platform or read flag
  bool generate_elf = /* platform detection or flag */;
  
  if (generate_elf) {
      ElfFileWriter writer;
      IRConverter<ElfFileWriter> converter(context, writer);
      // ... rest of compilation
  } else {
      ObjectFileWriter writer;
      IRConverter<ObjectFileWriter> converter(context, writer);
      // ... rest of compilation
  }
  ```

#### 5.3 Testing Strategy
- **Unit tests**: Test ELF file structure is valid
  - Use `readelf` to verify generated ELF files
  - Check sections, symbols, relocations
  
- **Integration tests**: 
  - Simple programs: `main() { return 0; }`
  - Test linking: `gcc -c` our .o file and link with gcc
  - Test execution: Run the resulting binary
  
- **Regression tests**:
  - Ensure COFF generation still works on Windows
  - All existing tests should pass

### Phase 6: Build System Updates

#### 6.1 Makefile
- **File**: `Makefile`
- **Add**: ELFIO include paths
- **Test**: Ensure builds work on Linux

#### 6.2 Windows Build
- **Files**: `FlashCpp.vcxproj`, `build_flashcpp.bat`
- **Ensure**: ELFIO headers don't break Windows builds
- **Strategy**: Header-only library should be fine on all platforms

## Implementation Order (Prioritized)

### Milestone 1: Basic Infrastructure (This PR) ✅
1. ✅ **Create this planning document**
2. ✅ **Add ELFIO library to `src/elfio/`** - Header-only library added with LICENSE
3. ✅ **Create skeletal `ElfFileWriter` class with basic structure**
4. ✅ **Basic .text, .data, .rodata sections working**
5. ✅ **Minimal symbol table implemented**
6. ⏳ Implement platform detection in build/main
7. ⏳ Test simple programs (return constant, global variables)
8. ⏳ Verify ELF files are valid and can be inspected with readelf/objdump

### Milestone 2: Linking Support (Next PR)
7. Implement relocations (PC32, 64-bit absolute)
8. Add string literals and global variables
9. Test simple programs (return constant, global variables)
10. Verify linking with GCC linker

### Milestone 3: Function Calls (Future PR)
11. Implement function symbols and relocations
12. Test function calls and returns
13. Add extern "C" support

### Milestone 4: C++ Features (Future PR)
14. Add Itanium name mangling
15. Implement vtables and RTTI for ELF
16. Test inheritance and virtual functions

### Milestone 5: Debug Information (Future PR)
17. Implement basic DWARF line info
18. Add minimal debug symbols
19. Test debugging with GDB

### Milestone 6: Exception Handling (Future)
20. Research Linux exception handling (`.eh_frame`)
21. Implement if needed (may defer further)

## Non-Goals (Out of Scope for Initial Implementation)

1. **Full ABI compatibility** - We're keeping existing calling conventions initially
2. **Position-independent code (PIC)** - Will use absolute addressing for MVP
3. **Shared library support** - Focus on static linking first
4. **Full DWARF debug info** - Minimal debug info only
5. **Exception handling on Linux** - Defer to future work
6. **Cross-compilation** - Linux→Windows or Windows→Linux (possible but not priority)

## Technical Decisions

### Decision 1: No Inheritance Hierarchy
- **Decision**: Create parallel `ElfFileWriter` instead of base class
- **Rationale**: 
  - Matches repository style (avoid inheritance)
  - Simpler code, easier to understand
  - Template-based selection is idiomatic C++
  - No runtime overhead from virtual functions

### Decision 2: Header-Only ELFIO Library
- **Decision**: Use ELFIO header-only library
- **Rationale**:
  - Similar to COFFI (already in use)
  - No linking dependencies
  - MIT licensed (compatible)
  - Well-maintained and used in production

### Decision 3: Separate Mangling Files
- **Decision**: `NameMangling.h` (MSVC) and `ItaniumMangling.h` (Linux)
- **Rationale**:
  - Two very different algorithms
  - Keep separation of concerns
  - Each file can be tested independently

### Decision 4: Minimal DWARF for MVP
- **Decision**: Basic DWARF only, defer full implementation
- **Rationale**:
  - DWARF is complex (hundreds of pages spec)
  - Can link without debug info
  - Incremental development approach
  - Line numbers + symbols good enough for MVP

## File Structure (After Implementation)

```
src/
├── coffi/              # Existing COFF library
│   └── coffi.hpp
├── elfio/              # New ELF library (to add)
│   └── elfio.hpp
├── ObjFileWriter.h     # Existing COFF writer
├── ElfFileWriter.h     # New ELF writer (to create)
├── NameMangling.h      # Existing MSVC mangling
├── ItaniumMangling.h   # New Itanium mangling (to create)
├── CodeViewDebug.h     # Existing CodeView debug (Windows)
├── DwarfDebug.h        # New DWARF debug (Linux) (to create)
├── IRConverter.h       # Modified to support both writers
└── main.cpp            # Modified for platform detection
```

## Testing Plan

### Phase 1 Tests: Basic Structure
```bash
# Test 1: Empty file compiles
echo "int main() { return 0; }" | ./FlashCpp - -o test.o
readelf -h test.o  # Verify ELF header
readelf -S test.o  # Verify sections

# Test 2: Link and run
gcc test.o -o test
./test
echo $?  # Should be 0
```

### Phase 2 Tests: Data and Symbols
```bash
# Test 3: Global variable
echo "int x = 42; int main() { return x; }" | ./FlashCpp - -o test.o
gcc test.o -o test
./test
echo $?  # Should be 42

# Test 4: String literal
echo 'const char* s = "hello"; int main() { return 0; }' | ./FlashCpp - -o test.o
readelf -p .rodata test.o  # Should show "hello"
```

### Phase 3 Tests: Function Calls
```bash
# Test 5: Simple function
cat > test.cpp << 'EOF'
int add(int a, int b) { return a + b; }
int main() { return add(1, 2); }
EOF
./FlashCpp test.cpp -o test.o
gcc test.o -o test
./test
echo $?  # Should be 3
```

## Resources and References

### ELF Specification
- ELF v1.2 specification (Tool Interface Standard)
- System V ABI AMD64 Architecture Processor Supplement
- LSB (Linux Standard Base) specification

### DWARF Specification
- DWARF v4/v5 specification
- GCC DWARF extensions

### Libraries
- **ELFIO**: https://github.com/serge1/ELFIO
- **COFFI**: https://github.com/serge1/COFFI (already in use)

### Name Mangling
- Itanium C++ ABI: https://itanium-cxx-abi.github.io/cxx-abi/abi.html
- GCC implementation: part of GCC codebase

### Tools for Verification
- `readelf`: Examine ELF files
- `objdump`: Disassemble and inspect
- `nm`: List symbols
- `ld`: GNU linker
- `gdb`: Debug generated code

## Risk Assessment

### High Risk
- **DWARF complexity**: May take longer than expected
  - **Mitigation**: Start with minimal implementation, iterate
- **Name mangling compatibility**: Itanium ABI is complex
  - **Mitigation**: Use C linkage for MVP, add C++ incrementally

### Medium Risk  
- **Relocation types**: Different semantics between COFF and ELF
  - **Mitigation**: Test thoroughly, use standard types first
- **Platform differences**: Subtle differences in behavior
  - **Mitigation**: Test on actual Linux system, not just WSL

### Low Risk
- **ELFIO integration**: Similar to COFFI (already working)
- **Build system**: Makefile already exists for Linux
- **Template-based design**: Already used in codebase

## Success Criteria

### Minimum Viable Product (MVP)
1. ✅ Generate valid ELF object files on Linux
2. ✅ Link with system linker (ld/gcc)
3. ✅ Execute simple programs (return values, global variables)
4. ✅ Support basic C-style functions (extern "C")
5. ✅ All existing COFF tests still pass

### Extended Goals (Future)
6. Full Itanium name mangling for C++
7. Virtual functions and RTTI work on Linux
8. Basic DWARF debug information
9. GDB can debug generated code
10. Exception handling on Linux

## Timeline Estimate

- **Milestone 1**: 1-2 days (infrastructure, skeleton)
- **Milestone 2**: 2-3 days (linking, basic tests)
- **Milestone 3**: 1-2 days (function calls)
- **Milestone 4**: 3-5 days (C++ features, mangling)
- **Milestone 5**: 3-5 days (DWARF debug info)

**Total for MVP (Milestones 1-3)**: ~1 week
**Total for C++ Support (+ Milestone 4)**: ~2 weeks
**Total for Debug Info (+ Milestone 5)**: ~3 weeks

## Conclusion

This plan provides a clear, incremental path to adding Linux ELF support to FlashCpp while maintaining the existing Windows COFF functionality. By following the repository's C-style philosophy and avoiding large class hierarchies, we can implement this feature cleanly and maintainably.

The key insight is using template-based duck typing rather than inheritance, which aligns with the codebase style and provides zero runtime overhead. Starting with a minimal implementation (C linkage, no debug info) allows us to deliver value incrementally while building toward full C++ support on Linux.
