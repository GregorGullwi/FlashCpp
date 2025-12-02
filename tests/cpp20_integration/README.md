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

| Test File | Clang++ 18 | GCC 13 | FlashCpp |
|-----------|-----------|---------|----------|
| cpp20_simple_integration_test.cpp | ✅ Pass (490/490) | ✅ Pass (490/490) | ⚠️ Has bugs |
| flashcpp_minimal_test.cpp | ✅ Pass (150/150) | ✅ Pass (150/150) | ⚠️ Has bugs |

## Known FlashCpp Limitations

Testing revealed several compiler bugs. See [`bugs/README.md`](bugs/README.md) for minimal reproduction cases.

### Critical Bugs (Blocking basic functionality)
1. **Boolean intermediate variables** - Crashes with assertion failure in IRTypes.h
2. **Namespace symbol lookup** - Symbol not found during code generation

### High Priority Bugs (Limiting advanced features)
3. **sizeof... operator** - Segmentation fault in variadic templates
4. **Template specialization** - Parser errors with `template<>` syntax

### Medium Priority Bugs (Workarounds available)
5. **if constexpr** - Code generation succeeds but linking fails

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
5. Create performance benchmarks
6. Add more edge cases and corner cases

## Conclusion

The integration tests successfully demonstrate:
- ✅ Comprehensive C++20 feature coverage (490 test points)
- ✅ Valid, standards-compliant code
- ✅ Modular, maintainable test structure
- ✅ Self-verifying test framework
- ✅ Identification of FlashCpp limitations for future improvement

The tests serve as both validation and documentation of FlashCpp's C++20 support while providing a foundation for future development.
