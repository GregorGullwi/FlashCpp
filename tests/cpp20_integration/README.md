# FlashCpp C++20 Integration Test Suite

## Overview

This directory contains comprehensive integration tests for the FlashCpp C++20 compiler. The test suite exercises **490 test points** across **9 major feature categories**, excluding coroutines, modules, and multithreading as requested.

## Quick Start

```bash
cd tests/cpp20_integration
./run_integration_test.sh
```

This will compile and run the integration test with clang++, displaying a detailed breakdown of all tested features.

## Test Files

### Main Integration Test
**`cpp20_simple_integration_test.cpp`** - Primary integration test (RECOMMENDED)
- **Status**: ✅ Fully working with standard C++20 compilers (clang++, g++)
- **Coverage**: 490 test points across 9 feature categories
- **Design**: Single-file, self-verifying (returns 0 on success)
- **Standards**: Valid, standards-compliant C++20 code

### Additional Test Files
- **`cpp20_integration_test.cpp`** - Advanced version with more features (some trigger FlashCpp bugs)
- **`flashcpp_minimal_test.cpp`** - Simplified version with workarounds for FlashCpp limitations

### Bug Reproduction Files
The `bugs/` directory contains minimal reproduction cases for FlashCpp compiler bugs discovered during testing. See [`bugs/README.md`](bugs/README.md) for details.

## Feature Coverage (490 Points)

### 1. Basic Types & Literals (30 points)
- Integer types: `char`, `short`, `int`, `long`, `unsigned` variants
- Floating-point: `float`, `double`
- Boolean and `nullptr`
- Hex literals

### 2. Operators (50 points)
- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Bitwise: `&`, `|`, `^`, `<<`, `>>`
- Logical: `&&`, `||`, `!`
- Comparison: `==`, `!=`, `<`, `>`, `<=`, `>=`
- Compound assignment: `+=`, `-=`, `*=`, `/=`
- Increment/decrement: `++`, `--`

### 3. Control Flow (50 points)
- `if`/`else` statements
- `for`, `while`, `do-while` loops
- `switch`/`case` statements
- `break` and `continue`

### 4. Functions (20 points)
- Function declarations and definitions
- Function overloading
- Function pointers
- Trailing return type: `auto func() -> type`

### 5. Classes and OOP (30 points)
- Basic class declarations
- Constructors and member functions
- Inheritance and virtual functions
- `new`/`delete` operators

### 6. Templates (30 points)
- Function templates with type deduction
- Class templates
- Class Template Argument Deduction (CTAD)
- Variadic templates
- Fold expressions (C++17)

### 7. Constexpr (10 points)
- Constexpr variables
- Constexpr functions
- Constexpr recursion (factorial)
- `static_assert`

### 8. Lambdas (10 points)
- Lambda expressions with no captures
- Lambda parameters
- Value and reference captures
- Immediately invoked lambdas (IIFE)

### 9. Modern C++ Features (60 points)
- `auto` type deduction
- `decltype`
- `typedef` and `using` declarations
- Enumerations and `enum class`
- Unions
- Designated initializers

### 10. Advanced Features (100 points)
- String literals
- Multi-dimensional arrays
- Pointer to pointer
- Complex struct initialization
- References and const references
- Ternary operator
- Nested structures
- Static variables
- Global variables

### 11. Alternative Tokens and C++20 Extras (100 points)
- Alternative operator representations (`and`, `or`, `not`, `bitand`, `bitor`, `xor`, `compl`)
- `sizeof` operator with various types
- Comma operator
- Advanced `nullptr` comparisons
- Explicit type casts (`static_cast`)
- Address-of (`&`) and dereference (`*`) operators
- Array subscript operator
- Octal literals
- Binary literals (C++14)
- Digit separators (C++14)

## Test Design

- **Single file**: All tests in `cpp20_simple_integration_test.cpp`
- **Modular**: Each feature category has its own test function
- **Point-based**: Each test returns 0 or 10 points
- **Self-verifying**: Returns 0 on complete success, failure count otherwise
- **Total**: 490 points across all categories

## Usage

### Running with Standard Compilers

```bash
# Compile and run with clang++
clang++ -std=c++20 cpp20_simple_integration_test.cpp -o test
./test
echo $?  # Should print 0 on success

# Or use the test script
./run_integration_test.sh
```

### Expected Results

- **Success**: Exit code 0 (all 490 points earned)
- **Partial Success**: Exit code = (490 - points_earned)
- **Failure**: Exit code > 0 indicates number of points missed

## Verification Results

### Standard Compilers

| Test File | Clang++ 18.1.3 | GCC 13.3.0 |
|-----------|----------------|------------|
| cpp20_simple_integration_test.cpp | ✅ Pass (490/490) | ✅ Pass (490/490) |

### FlashCpp Results (Full Integration Test)

Testing performed by compiling the full integration test with FlashCpp debug build,
linking with clang, and running the resulting binary. Tested on Linux x86-64,
clang++ 18.1.3 linker.

| Section | Feature | Points | FlashCpp Status |
|---------|---------|--------|-----------------|
| 1 | Basic Types & Literals | 30 | ✅ Pass (30/30) |
| 2 | Operators | 50 | ✅ Pass (50/50) |
| 3 | Control Flow | 50 | ✅ Pass (50/50) |
| 4 | Functions | 20 | ✅ Pass (20/20) |
| 5 | Classes and OOP | 30 | ✅ Pass (30/30) |
| 6 | Templates | 30 | ✅ Pass (30/30) - function/class templates, fold expressions, variadic recursion, and if constexpr all work |
| 7 | Constexpr | 10 | ✅ Pass (10/10) |
| 8 | Lambdas | 10 | ✅ Pass (10/10) |
| 9 | Modern C++ Features | 60 | ✅ Pass (60/60) |
| 10 | Advanced Features | 100 | ✅ Pass (100/100) |
| 11 | Alt Tokens & C++20 Extras | 100 | ⚠️ Pass (90/100) - `test_explicit_casts` fails: `static_cast<bool>(n)` stores raw int value instead of normalizing to 0/1 |

**Overall**: FlashCpp passes **480/490 points** (98% pass rate).

## Compilation Speed Benchmarks

Benchmarks measured end-to-end compile time (source to object file) for the integration
test (~860 lines) on Linux x86-64. 20 iterations each. Run `./run_benchmark.sh` to reproduce.

| Compiler | Avg (ms) | Min (ms) | Max (ms) |
|----------|----------|----------|----------|
| **FlashCpp (release, -O3)** | **75** | **72** | **84** |
| Clang++ 18.1.3 -O0 | 91 | 84 | 100 |
| Clang++ 18.1.3 -O2 | 102 | 96 | 109 |
| GCC 13.3.0 -O0 | 105 | 94 | 121 |
| FlashCpp (debug, -g) | 119 | 113 | 130 |
| GCC 13.3.0 -O2 | 119 | 109 | 180 |

FlashCpp's internal timing breakdown (debug build, ~53ms actual work, rest is
process startup overhead):

| Phase | Time (ms) | Percentage |
|-------|-----------|------------|
| Preprocessing | 2.0 | 4% |
| Lexer Setup | 0.3 | 1% |
| Parsing | 30.3 | 57% |
| IR Conversion | 6.4 | 12% |
| Code Generation | 11.5 | 22% |
| Other | 2.2 | 4% |
| **TOTAL** | **~52.7** | 100% |

**Key observations**:
- FlashCpp release build is the **fastest compiler tested** (75ms avg)
- 18% faster than Clang -O0 (91ms) and 29% faster than GCC -O0 (105ms)
- Even the debug build (119ms) is competitive with GCC and Clang -O2
- FlashCpp performs full compilation (preprocess + parse + codegen + ELF output) in a single pass
- The Clang/GCC numbers use `-O0` (no optimization), which is the default for `clang++ -c`

## Known FlashCpp Limitations

All 9 originally reported bugs have been fixed. See [`bugs/README.md`](bugs/README.md) for details.

### Remaining Limitations
- **`static_cast<bool>(n)` normalization** - Stores raw integer value instead of normalizing to 0 or 1. Causes `test_explicit_casts` to fail when `static_cast<bool>(42)` produces 42 (non-zero but ≠1), which breaks logical AND with comparison results.

## Features NOT Tested

As requested in the problem statement:
- ❌ Coroutines (`co_await`, `co_yield`, `co_return`)
- ❌ Modules (`import`, `export`, `module`)
- ❌ Multithreading (`std::thread`, `std::mutex`, `std::atomic`)

Additionally excluded due to FlashCpp limitations:
- Range-based for loops (not yet implemented)
- std library headers (FlashCpp doesn't include standard library)
- Concepts with complex constraints (partial support only)

## Test Philosophy

1. **Standards Compliance**: All tests are valid C++20 code that compiles with standard compilers
2. **Self-Verifying**: Tests return 0 on success, non-zero indicating number of failures
3. **Modular**: Each feature category tested independently
4. **Point-Based Scoring**: Each test function returns 0 or 10 points
5. **Single File**: Main test logic in one .cpp for simplicity
6. **No External Dependencies**: Tests don't rely on std library

## Contributing

When extending the test:

1. **Test with standard compiler first**:
   ```bash
   clang++ -std=c++20 your_test.cpp -o test && ./test
   ```

2. **Verify it's valid C++20**: The test should compile and run correctly
3. **Test incremental complexity**: Start simple, add complexity gradually
4. **Document FlashCpp-specific issues**: If FlashCpp can't compile valid C++20, create a bug file
5. **Keep tests focused**: Each test function should test one feature/concept
6. **Update totals**: Adjust expected values when adding/removing tests

## Test Purpose

The integration test serves multiple purposes:
1. **Validation**: Proves FlashCpp can compile real C++20 code
2. **Regression Testing**: Catches when new changes break existing features
3. **Documentation**: Shows which C++20 features are supported
4. **Benchmarking**: Provides a comprehensive compilation test
5. **Bug Discovery**: Identifies compiler limitations for future improvement

## Documentation Files

- **`README.md`** (this file) - Comprehensive test documentation
- **`QUICKSTART.md`** - Quick reference guide for users
- **`bugs/README.md`** - Known bugs with minimal reproduction cases

## Future Enhancements

Planned improvements:
1. Add exception handling tests when implemented
2. Add more RTTI tests (typeid, dynamic_cast)
3. Add spaceship operator tests when fully implemented
4. Add concept tests when FlashCpp concepts are more stable
5. Add more edge cases and corner cases
6. Track benchmark improvements over time

## Conclusion

The integration tests successfully demonstrate:
- ✅ Comprehensive C++20 feature coverage (490 test points)
- ✅ Valid, standards-compliant code
- ✅ Modular, maintainable test structure
- ✅ Self-verifying test framework
- ✅ All 9 originally reported bugs fixed
- ✅ 480/490 points passing (98% pass rate) in full combined test
- ✅ FlashCpp release build is the fastest compiler tested (75ms avg vs 91ms Clang -O0)

The tests serve as both validation and documentation of FlashCpp's C++20 support while providing a foundation for future development.
