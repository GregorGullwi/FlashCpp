# FlashCpp C++20 Integration Test

## Overview

This comprehensive integration test exercises as many C++20 features as possible in FlashCpp, excluding coroutines, modules, and multithreading.

## Test Structure

The test is organized into 9 major sections, each testing a category of C++20 features:

### Section 1: Basic Types and Literals (30 points)
- Integer types (char, short, int, long, unsigned variants)
- Floating-point types (float, double)
- Boolean type and nullptr
- Hex literals

### Section 2: Operators (50 points)
- Arithmetic operators (+, -, *, /, %)
- Bitwise operators (&, |, ^, <<, >>)
- Logical operators (&&, ||, !)
- Comparison operators (==, !=, <, >, <=, >=)
- Compound assignment (+=, -=, *=, /=)
- Increment/decrement (++, --)

### Section 3: Control Flow (50 points)
- If/else statements
- For loops
- While loops
- Do-while loops
- Switch/case statements
- Break and continue

### Section 4: Functions (20 points)
- Function declarations and definitions
- Function parameters and return values
- Function overloading
- Function pointers
- Trailing return type syntax

### Section 5: Classes and OOP (30 points)
- Basic class declarations
- Constructors and member functions
- Inheritance and virtual functions
- new/delete operators
- Virtual function dispatch

### Section 6: Templates (30 points)
- Function templates with type deduction
- Class templates
- Class Template Argument Deduction (CTAD)
- Variadic templates
- Fold expressions (C++17)

### Section 7: Constexpr (10 points)
- Constexpr variables
- Constexpr functions
- Constexpr recursion (factorial)
- static_assert

### Section 8: Lambdas (10 points)
- Lambda expressions with no captures
- Lambda parameters
- Value captures
- Reference captures
- Immediately invoked lambdas

### Section 9: Modern C++ Features (60 points)
- Auto type deduction
- Decltype
- Typedef and using declarations
- Enumerations and enum classes
- Unions
- Designated initializers

## Running the Test

### Quick Start with Standard Clang++

To verify the test is valid C++20 code:

```bash
cd tests/cpp20_integration
clang++ -std=c++20 cpp20_simple_integration_test.cpp -o test.exe
./test.exe
echo $?  # Should print 0 on success
```

### Using the Test Script

```bash
cd tests/cpp20_integration
./run_integration_test.sh
```

This script will:
1. Attempt to compile with FlashCpp (if available)
2. Verify with standard clang++
3. Run the test and report results

## Test Design

- **Single file**: All tests are in `cpp20_simple_integration_test.cpp`
- **Modular**: Each feature category has its own test function
- **Point-based**: Each test function returns 0 or 10 points
- **Self-verifying**: main() returns 0 on complete success, or the number of missing points on failure
- **Total: 290 points** across all categories

## Expected Results

- **Success**: Exit code 0 (all 290 points earned)
- **Partial Success**: Exit code = (290 - points_earned)
- **Failure**: Exit code > 0 indicates number of points missed

## Features NOT Tested

As requested in the problem statement, the following are explicitly excluded:
- Coroutines (co_await, co_yield, co_return)
- Modules (import, export, module)
- Multithreading (std::thread, std::mutex, std::atomic)

Additionally, some advanced features are not tested due to current FlashCpp limitations:
- Namespaces with qualified lookup (causes symbol table issues)
- Concepts with requires expressions (some limitations)
- Template specialization (parser issues)
- Range-based for loops (not yet implemented)
- std library headers (FlashCpp doesn't include standard library)

## Test File Compatibility

The test file `cpp20_simple_integration_test.cpp` is designed to:
1. **Compile with standard C++20 compilers** (clang++, g++, MSVC)
2. **Be as compatible as possible with FlashCpp's current feature set**
3. **Avoid features that crash the FlashCpp compiler**
4. **Test real C++20 features, not workarounds**

## Known Issues with FlashCpp

Based on testing, the following FlashCpp issues were identified:

1. **Namespace symbol lookup**: Using namespace variables causes symbol table crashes
2. **sizeof... operator**: Causes crashes in variadic template expansion
3. **Template specialization**: Parser errors with explicit specialization syntax
4. **Default function arguments**: Not fully supported
5. **if constexpr**: Causes compilation issues in some contexts

These are documented separately and should be addressed in FlashCpp development.

## Future Enhancements

Possible additions to the integration test:
- Exception handling (try/catch/throw) when supported
- RTTI (typeid, dynamic_cast) more extensively
- Spaceship operator (<=>)  when fully implemented
- More template metaprogramming when stable
- Concepts with complex constraints when working

## Verification

This integration test has been verified to:
- ✅ Compile successfully with clang++ 18.0+
- ✅ Compile successfully with g++ 13.0+
- ✅ Run correctly and return exit code 0
- ✅ Exercise 290 test points across 9 feature categories
- ✅ Cover the majority of C++20 features supported by FlashCpp

## Contributing

When adding new tests:
1. Add them to the appropriate section
2. Make each test function return 0 or 10 points
3. Update the expected total in main()
4. Verify with both clang++ and FlashCpp
5. Update this README with the new feature coverage
