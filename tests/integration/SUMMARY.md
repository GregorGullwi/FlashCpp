# FlashCpp Integration Test Summary

## Purpose

This directory contains comprehensive integration tests for FlashCpp, a C++20 compiler. The tests are designed to verify that FlashCpp correctly compiles and executes C++20 code across a wide range of language features.

## Test Files

### 1. `cpp20_simple_integration_test.cpp` (RECOMMENDED)
**Status**: ✅ Fully working with standard C++20 compilers (clang++, g++)

This is the **primary integration test** and represents valid, standards-compliant C++20 code.

**Coverage**:
- 290 test points across 9 feature categories
- All major C++20 features except coroutines, modules, and multithreading
- Self-verifying (returns 0 on success)

**How to run**:
```bash
# With standard clang++
clang++ -std=c++20 cpp20_simple_integration_test.cpp -o test.exe
./test.exe
echo $?  # Should print 0

# Or use the script
./run_integration_test.sh
```

**Feature Coverage**:
1. Basic Types & Literals (30 pts) - int, float, double, bool, nullptr, hex
2. Operators (50 pts) - arithmetic, bitwise, logical, comparison, compound
3. Control Flow (50 pts) - if/else, for, while, do-while, switch, break/continue
4. Functions (20 pts) - declarations, overloading, function pointers, trailing return
5. Classes/OOP (30 pts) - inheritance, virtual functions, new/delete
6. Templates (30 pts) - function, class, CTAD, variadic, fold expressions
7. Constexpr (10 pts) - variables, functions, recursion, static_assert
8. Lambdas (10 pts) - captures (value/reference), parameters, IIFE
9. Modern Features (60 pts) - auto, decltype, typedef, using, enums, unions, designated init

### 2. `flashcpp_minimal_test.cpp`
**Status**: ⚠️ Partial - works with clang++ but FlashCpp has known bugs

This is a simplified version designed to work around FlashCpp's current limitations.

**Known FlashCpp Issues** (as of Dec 2, 2025):
- Boolean intermediate variables with complex logic cause crashes
- Some template instantiations cause assertion failures
- Namespace symbol lookup has issues
- sizeof... operator not fully implemented

### 3. `cpp20_integration_test.cpp`
**Status**: 🚧 Development version with more advanced features

This was the initial comprehensive test but revealed several FlashCpp limitations.

## Test Philosophy

The integration tests follow these principles:

1. **Standards Compliance**: All tests are valid C++20 code that compiles with standard compilers
2. **Self-Verifying**: Tests return 0 on success, non-zero indicating number of failures
3. **Modular**: Each feature category is tested independently
4. **Point-Based Scoring**: Each test function returns 0 or 10 points
5. **Single File**: Main test logic in one .cpp file for simplicity
6. **No External Dependencies**: Tests don't rely on std library (FlashCpp doesn't include it)

## Recommendations for FlashCpp Users

### What Works Well ✅
Based on testing, these features work reliably in FlashCpp:
- Basic arithmetic and floating-point operations
- Simple control flow (if/else, for, while)
- Classes with inheritance and virtual functions
- Basic templates (function and class)
- Simple lambda expressions
- Auto type deduction (simple cases)

### What to Avoid ⚠️
These features currently have issues in FlashCpp:
- Complex boolean expressions with intermediate variables
- Namespace qualified names
- sizeof... in variadic templates
- Template specialization (some cases)
- if constexpr in templates

## Contributing

When adding new tests:

1. **Test with standard compiler first**:
   ```bash
   clang++ -std=c++20 your_test.cpp -o test && ./test
   ```

2. **Verify it's valid C++20**: The test should compile and run correctly
3. **Test incremental complexity**: Start simple, add complexity gradually
4. **Document FlashCpp-specific issues**: If FlashCpp can't compile valid C++20, document why
5. **Keep tests focused**: Each test function should test one feature/concept
6. **Update totals**: Adjust expected values when adding/removing tests

## Expected vs Actual

### cpp20_simple_integration_test.cpp
- **Expected**: 290 points (all tests pass)
- **With clang++**: 290/290 ✅
- **With FlashCpp**: Testing reveals compiler bugs, see issues above

### Design Goal
The integration test serves multiple purposes:
1. **Validation**: Proves FlashCpp can compile real C++20 code
2. **Regression Testing**: Catches when new changes break existing features
3. **Documentation**: Shows which C++20 features are supported
4. **Benchmarking**: Provides a comprehensive compilation test

## Future Work

Planned improvements:
1. Create FlashCpp-specific test that works around known bugs
2. Add concept tests when FlashCpp concepts are more stable
3. Add exception handling tests when implemented
4. Add more RTTI tests (typeid, dynamic_cast)
5. Add spaceship operator tests when implemented
6. Create performance benchmarks

## Verification Results

| Test File | Clang++ 18 | GCC 13 | FlashCpp |
|-----------|-----------|---------|----------|
| cpp20_simple_integration_test.cpp | ✅ Pass | ✅ Pass | ⚠️ Has bugs |
| flashcpp_minimal_test.cpp | ✅ Pass | ✅ Pass | ⚠️ Has bugs |

## Bug Reports

When FlashCpp fails to compile valid C++20 code from these tests, create bug reports with:
1. The specific test function that fails
2. The error message or crash log
3. Expected behavior (how clang++ handles it)
4. Minimal reproduction case

## Conclusion

The integration tests successfully demonstrate:
- ✅ Comprehensive C++20 feature coverage (290 test points)
- ✅ Valid, standards-compliant code  
- ✅ Modular, maintainable test structure
- ✅ Self-verifying test framework
- ⚠️ Identification of FlashCpp limitations for future improvement

The tests serve as both validation and documentation of FlashCpp's C++20 support.
