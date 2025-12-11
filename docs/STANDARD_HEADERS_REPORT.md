# Standard Headers Support Report for FlashCpp

## Overview

This document reports on the current state of standard C++ header support in FlashCpp and identifies the issues that need to be fixed to enable standard library inclusion.

## Test Results Summary

Standard headers now pass the preprocessor stage and most parsing. Many core template features are now complete including template partial specialization with inheritance.

### Compiler Mode

FlashCpp defaults to **MSVC mode** for Windows compatibility. Use `-fgcc-compat` or `-fclang-compat` to enable GCC/Clang mode:

**MSVC Mode (default)**:
- `__SIZE_TYPE__` = `unsigned __int64` (and related MSVC x64 types)
- `__WCHAR_TYPE__` = `unsigned short`
- Compatible with Windows standard library headers

**GCC/Clang Mode** (enabled with `-fgcc-compat` or `-fclang-compat`):
- `__SIZE_TYPE__` = `long unsigned int` (and related GCC/Clang x64 types)
- `__WCHAR_TYPE__` = `int`
- Compatible with Linux/macOS standard library headers

Both modes support:
- `__attribute__((...))` syntax (parser skips these)
- `noexcept` specifier
- `#pragma GCC` directives (preprocessor ignores these)

### Fixed Issues

#### Issue 1: Undefined `__` Prefixed Identifiers in Preprocessor Conditions (FIXED)

**Location**: `src/FileReader.h`, `evaluate_expression()` function, around line 1288

**Problem**: When the preprocessor encounters an identifier starting with `__` in a `#if` directive (like `__cpp_exceptions`), it only handles `__has_include` specially. All other `__` prefixed identifiers that aren't defined as macros don't push any value to the values stack, causing a "values stack is empty" error.

**Fix Applied**: Added an `else` clause that treats unknown `__` identifiers as undefined (pushing 0):

```cpp
if (keyword.find("__") == 0) {  // __ is reserved for the compiler
    if (keyword.find("__has_include") == 0) {
        // ... existing __has_include handling ...
    }
    else {
        // Unknown __ identifier - treat as 0 (undefined)
        values.push(0);
    }
}
```

#### Issue 1b: Missing Arithmetic and Bitwise Operators in Preprocessor (FIXED)

**Problem**: The preprocessor expression evaluator didn't support arithmetic operators (`+`, `-`, `*`, `/`, `%`) or bitwise operators (`<<`, `>>`, `&`, `|`, `^`, `~`). These are used in standard library headers for version checks like `((2) << 16) + (15)`.

**Fix Applied**: Added support for all arithmetic and bitwise operators in `evaluate_expression()` and `apply_operator()`.

#### Issue 1c: GCC Builtin Type Macros (FIXED)

**Problem**: Standard headers use compiler builtin type macros like `__SIZE_TYPE__`, `__PTRDIFF_TYPE__`, etc.

**Fix Applied**: Added 35+ GCC/Clang builtin type macros when GCC compatibility mode is enabled.

#### Issue 1d: GCC Attributes and Noexcept (FIXED)

**Problem**: Standard headers use `__attribute__((...))` and `noexcept` specifiers.

**Fix Applied**: Added `skip_gcc_attributes()` and `skip_function_trailing_specifiers()` in Parser.cpp.

#### Issue 1e: Template Partial Specialization with Inheritance (FIXED)

**Problem**: The parser needed to handle partial template specialization with inheritance:
```cpp
template<typename T> struct MyType<const T> : MyType<T> { };
```

**Fix Applied**: The parser now properly handles inheritance in partial specializations (lines 16195-16300 in Parser.cpp). When a partial specialization includes a base class list (`:` followed by base classes), it correctly parses the inheritance and sets up the type hierarchy.

**Test Cases**: 
- `tests/test_partial_spec_inherit.cpp` - PASSES
- `tests/test_partial_spec_inherit_simple.cpp` - PASSES  
- `tests/template_partial_specialization_test.cpp` - PASSES

**Status**: ✅ COMPLETE - This pattern is now fully supported and critical for standard library headers like `<cstddef>`.

### Remaining Issues

#### Issue 2: Missing C++ Feature Test Macros (FIXED)

**Problem**: The standard library headers rely heavily on C++ feature test macros to enable/disable features based on compiler/language support. These need to be predefined in the preprocessor.

**Fix Applied**: All essential C++ feature test macros are now defined in `FileReader.h`, `addBuiltinDefines()` method (lines 1903-1923):

| Macro | Value | Description |
|-------|-------|-------------|
| `__cpp_exceptions` | 199711L | Exception support |
| `__cpp_rtti` | 199711L | RTTI support |
| `__cpp_noexcept_function_type` | 201510L | C++17 noexcept in function type |
| `__cpp_constexpr` | 201603L | C++17 constexpr (relaxed) |
| `__cpp_concepts` | 202002L | C++20 concepts |
| `__cpp_if_constexpr` | 201606L | C++17 if constexpr |
| `__cpp_inline_variables` | 201606L | C++17 inline variables |
| `__cpp_structured_bindings` | 201606L | C++17 structured bindings |
| `__cpp_variadic_templates` | 200704L | Variadic templates |
| `__cpp_static_assert` | 201411L | C++17 static_assert with message |
| `__cpp_decltype` | 200707L | decltype |
| `__cpp_range_based_for` | 200907L | Range-based for |
| `__cpp_lambdas` | 200907L | Lambda expressions |
| `__cpp_initializer_lists` | 200806L | Initializer lists |
| `__cpp_delegating_constructors` | 200604L | Delegating constructors |
| `__cpp_nullptr` | 200704L | nullptr |
| `__cpp_auto_type` | 200606L | auto type |
| `__cpp_aggregate_bases` | 201603L | C++17 aggregate base classes |

**Status**: ✅ COMPLETE - All essential feature test macros are now defined.

#### Issue 3: Missing Compiler Intrinsic Functions

**Problem**: The standard library uses compiler intrinsic functions for feature detection.

**Required intrinsics**:

| Intrinsic | Description | Suggested Implementation |
|-----------|-------------|-------------------------|
| `__has_feature(x)` | Clang feature test | Return 0 for all features (or implement per-feature) |
| `__has_builtin(x)` | Test for builtin availability | Return 0 or implement specific builtins |
| `__has_cpp_attribute(x)` | Test for C++ attribute support | Return appropriate version numbers |
| `__has_extension(x)` | Test for language extensions | Return 0 |
| `__has_attribute(x)` | Test for __attribute__ support | Return 0 or implement |

#### Issue 4: Missing Sanitizer Macros

**Problem**: The standard library checks for sanitizer modes.

**Macros to handle** (should evaluate to 0 when not in sanitizer mode):
- `__SANITIZE_THREAD__`
- `__SANITIZE_ADDRESS__`
- `__SANITIZE_UNDEFINED__`

#### Issue 5: Missing Compiler Builtin Types (CURRENT BLOCKER)

**Problem**: The standard library headers use compiler-specific builtin types like:
- `__SIZE_TYPE__` - the underlying type for `size_t`
- `__PTRDIFF_TYPE__` - the underlying type for `ptrdiff_t`
- `__WCHAR_TYPE__` - the underlying type for `wchar_t`
- `__INT8_TYPE__`, `__INT16_TYPE__`, etc.

These need to be defined as preprocessor macros or handled specially in the parser.

**Example failing code** (from `bits/c++config.h:58`):
```cpp
typedef __SIZE_TYPE__ 	size_t;
```

**Potential Fix**: Add these as preprocessor defines in `addBuiltinDefines()`:
```cpp
defines_["__SIZE_TYPE__"] = DefineDirective{ "unsigned long", {} };
defines_["__PTRDIFF_TYPE__"] = DefineDirective{ "long", {} };
// etc.
```

### How to Reproduce

Run the test script:
```bash
cd /home/runner/work/FlashCpp/FlashCpp
./tests/test_standard_headers.sh
```

Or run the doctest test case:
```bash
./x64/Test/test --test-case="StandardHeaders:DiagnosticReport"
```

### Recommended Fix Order

1. ~~**First Priority**: Fix the `evaluate_expression()` bug where `__` prefixed identifiers don't push a value. This is the immediate crash cause.~~ **DONE**

2. ~~**Second Priority**: Add compiler builtin types (`__SIZE_TYPE__`, etc.) as preprocessor macros.~~ **DONE**

3. ~~**Third Priority**: Template partial specialization with inheritance.~~ **DONE**

4. ~~**Fourth Priority**: Add basic feature test macros (`__cpp_exceptions`, `__cpp_rtti`, `__cpp_constexpr`, etc.) to `addBuiltinDefines()`.~~ **DONE**

5. **Fifth Priority (Current)**: Implement `__has_feature`, `__has_builtin`, etc. as preprocessor intrinsics.

6. **Sixth Priority**: Add sanitizer macro handling.

### Test File Location

The diagnostic test case is located at:
- `tests/FlashCppTest/FlashCppTest/FlashCppTest/FlashCppTest.cpp` (TEST_CASE "StandardHeaders:DiagnosticReport")

### Additional Notes

- The test shell script `tests/test_standard_headers.sh` provides a comprehensive test of all major standard headers
- ~~Once the preprocessor issues are fixed, additional parser and semantic issues may be discovered~~ The preprocessor issues have been fixed; headers now fail at the parser stage
- Standard library support is a significant undertaking that will require ongoing development

## Headers Tested

The following headers were tested (currently fail at the parser stage due to missing `__SIZE_TYPE__` etc.):

### C Library Wrappers
- `<cstddef>`, `<cstdint>`, `<cstdlib>`, `<cstring>`, `<climits>`, `<cstdio>`, `<cmath>`, `<cassert>`, `<cerrno>`, `<cfloat>`

### C++ Utilities
- `<utility>`, `<type_traits>`, `<limits>`, `<initializer_list>`, `<tuple>`, `<any>`, `<optional>`, `<variant>`

### Containers
- `<array>`, `<vector>`, `<deque>`, `<list>`, `<forward_list>`, `<set>`, `<map>`, `<unordered_set>`, `<unordered_map>`, `<stack>`, `<queue>`

### Strings
- `<string>`, `<string_view>`

### I/O
- `<iostream>`, `<istream>`, `<ostream>`, `<sstream>`, `<fstream>`

### Memory
- `<memory>`, `<new>`

### Algorithms
- `<algorithm>`, `<functional>`, `<numeric>`, `<iterator>`

### Time
- `<chrono>`

### C++20 Features
- `<concepts>`, `<ranges>`, `<format>`, `<coroutine>`, `<span>`, `<bit>`, `<compare>`, `<source_location>`, `<version>`

### Threading
- `<thread>`, `<mutex>`, `<atomic>`, `<condition_variable>`, `<future>`

### Other
- `<filesystem>`, `<regex>`, `<exception>`, `<stdexcept>`
