## Flash C++ Compiler

**⚠️ IMPORTANT: This compiler is under heavy development and NOT ready for production use!**

Flash C++ Compiler is a modern C++20 compiler front-end focused on fast compilation and optimized code generation. This project is currently in active development and should be considered experimental. Many C++20 features are missing or incomplete.

---

## 🚀 Getting Started

### Building the Compiler

#### Windows (MSVC)
```bash
.\build_flashcpp.bat          # Build the compiler
```

#### Linux/WSL (Clang)
```bash
# Note: setup.sh is for Replit environment only - regular users should skip this
make main CXX=clang++         # Build compiler
```

### Running Tests

#### Windows (PowerShell)
```powershell
# Run the comprehensive test suite
.\tests\test_reference_files.ps1

# Analyze object file symbols
.\tests\analyze_obj_symbols.ps1
```

#### Linux/WSL (Bash)
```bash
# Run all tests
./tests/run_all_tests.sh

# Build and run specific test
make test && ./x64/test
```

### Basic Usage
```bash
# Compile a C++ file
./x64/Debug/FlashCpp.exe input.cpp -o output.obj

# Verbose mode (shows parsing and IR generation)
./x64/Debug/FlashCpp.exe -v input.cpp

# Set log level for debugging
./x64/Debug/FlashCpp.exe --log-level=Parser:trace input.cpp
```

---

## 📋 Command Line Options

| Option | Description |
|--------|-------------|
| `-v` | Verbose output (shows dependency analysis and IR) |
| `--log-level=level` | Set global log level (error, warning, info, debug, trace, or 0-4) |
| `--log-level=category:level` | Set log level for specific category (Parser, Lexer, Codegen, etc.) |
| `-o output.obj` | Specify output object file |
| `--help` | Show help message |
| `-I path` | Add include directory |
| `-E` | Preprocess only (output preprocessed source) |

---

## 🧪 Testing

The compiler includes 600+ test cases covering:

- **Basic operations**: Integer/float arithmetic, comparisons, bitwise ops
- **Control flow**: if/else, loops, switch statements, goto
- **OOP features**: Classes, inheritance, virtual functions, RTTI
- **Modern C++**: Namespaces, auto deduction, lambdas, templates
- **C++20 features**: Spaceship operator, concepts, delayed parsing

### Test Scripts

**Windows (PowerShell):**
- [`tests\analyze_obj_symbols.ps1`](tests/analyze_obj_symbols.ps1) - Analyze object file symbols
- [`tests\test_reference_files.ps1`](tests/test_reference_files.ps1) - Run comprehensive test suite

**Linux/WSL (Bash):**
- [`tests/run_all_tests.sh`](tests/run_all_tests.sh) - Run all tests on Linux

**Batch Files:**
- [`link_and_run_test_debug.bat`](link_and_run_test_debug.bat) - Link and run a single test executable
- [`link_and_run_add.bat`](link_and_run_add.bat) - Link and run addition test
- [`link_and_run_arithmetic.bat`](link_and_run_arithmetic.bat) - Link and run arithmetic test

---

## ✅ Implemented Features

### Core Language Support
- **Complete operator support**: 98% of C++ operators including spaceship `<=>`
- **Type system**: Integer, floating-point, boolean, pointer types
- **Control flow**: if/else, for/while/do-while, switch, goto
- **Functions**: Declarations, calls, function pointers
- **OOP**: Classes, inheritance, virtual functions, RTTI
- **Namespaces**: Full support with qualified lookup
- **Templates**: ~85% complete (class/function templates, specialization)

### C++20 Features
- **Spaceship operator**: `<=>` with automatic comparison synthesis
- **Concepts**: Basic constraint and requires clause support
- **Delayed parsing**: C++20 compliant inline member functions
- **Type traits**: 37 compiler intrinsics for `<type_traits>`

### Code Generation
- **x86-64 assembly**: Complete machine code generation
- **SSE/AVX2 optimizations**: Modern SIMD instructions
- **COFF/ELF output**: Object file generation

---

## ❌ Missing Features

For a complete list of missing features, see [`docs/MISSING_FEATURES.md`](docs/MISSING_FEATURES.md).

### High Priority Missing Features:
- **Range-based for loops**: `for (auto x : container)` syntax
- **Variable template constexpr initialization**: Compile-time evaluation
- **Template codegen fixes**: Out-of-line member definitions, if constexpr
- **Standard library headers**: Limited support for `<type_traits>`, `<utility>`

### Not Implemented:
- **Modules**: C++20 module system
- **Coroutines**: `co_await`, `co_yield`, `co_return`
- **Ranges library**: `std::ranges` adaptors and views

---

## 🏗️ Architecture

### Compilation Pipeline
1. **Preprocessor**: Macro expansion, conditional compilation
2. **Lexer**: Token recognition with full operator support
3. **Parser**: AST construction with C++20 compliance
4. **Semantic Analysis**: Type checking and promotion
5. **IR Generation**: LLVM-style intermediate representation
6. **Code Generation**: x86-64 assembly with optimizations
7. **Object File**: COFF/ELF format output

### Key Components
- **Parser**: `src/Parser.cpp` - Recursive descent with operator precedence
- **Code Generator**: `src/CodeGen.h` - AST to IR translation
- **IR Converter**: `src/IRConverter.h` - IR to x86-64 assembly
- **Template System**: `src/TemplateRegistry.h` - Template instantiation

---

## 🔧 Development

### Project Structure
```
src/          # Core compiler source code
tests/        # Test suite (222+ test cases)
docs/         # Documentation including missing features
x64/          # Generated binaries (untracked)
```

### Contributing
1. Implement missing features (see [`docs/MISSING_FEATURES.md`](docs/MISSING_FEATURES.md))
2. Add test cases in `tests/`
3. Update documentation
4. Verify assembly output quality

---

## 📈 Performance

- **Compile speed**: Optimized for fast compilation
- **Code quality**: Efficient x86-64 assembly generation
- **Test coverage**: 600+ test cases ensuring correctness

---

## 🔗 Links

- **Repository**: [GitHub - GregorGullwi/FlashCpp](https://github.com/GregorGullwi/FlashCpp)
- **Documentation**: See `docs/` directory for detailed feature analysis
- **Standard Headers Report**: [`docs/STANDARD_HEADERS_REPORT.md`](docs/STANDARD_HEADERS_REPORT.md)

---

## 🎯 Current Focus

**⚠️ WARNING: This compiler is NOT production-ready!** It's currently under heavy development and lacks many essential C++20 features. The compiler is suitable for experimentation and development purposes only.

Current development priorities:

1. **Fix template codegen issues** (out-of-line definitions, if constexpr)
2. **Implement range-based for loops**
3. **Enhance standard library header support**
4. **Complete variable template constexpr initialization**

See [`docs/NEXT_FEATURES.md`](docs/NEXT_FEATURES.md) for detailed roadmap.

---

## 🌐 Platform Support

### Object File Format Selection

The compiler automatically selects the appropriate object file format based on the platform:

- **Windows**: COFF format (Common Object File Format)
- **Linux/Unix**: ELF format (Executable and Linkable Format)

This selection is automatic based on compile-time platform detection. Currently there are no command-line flags to override this behavior.

### Cross-Platform Considerations

- **ABI Differences**: Windows (MSVC ABI) vs Linux (Itanium C++ ABI)
- **Exception Handling**: SEH on Windows, DWARF on Linux
- **Calling Conventions**: Windows x64 vs System V AMD64
- **RTTI Layout**: Different RTTI structures between platforms

For cross-platform development, compile separately for each target platform.

---

## 📞 Contact

**Maintainer**: Gregor Gullwi
**Twitter/X**: [@GregorGullwi](https://twitter.com/GregorGullwi)

For questions, bug reports, or contributions, please contact the maintainer on Twitter/X or open an issue on GitHub.
