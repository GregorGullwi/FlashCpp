# Standard Headers Support Report for FlashCpp

## Overview

This document reports on the current state of standard C++ header support in FlashCpp and identifies the issues that need to be fixed to enable standard library inclusion.

## Test Results Summary

All standard headers currently fail to include due to issues in the preprocessor when handling `bits/c++config.h`, which is the foundational configuration header included by all other standard headers.

### Key Issues Identified

#### Issue 1: Undefined `__` Prefixed Identifiers in Preprocessor Conditions

**Location**: `src/FileReader.h`, `evaluate_expression()` function, around line 1288

**Problem**: When the preprocessor encounters an identifier starting with `__` in a `#if` directive (like `__cpp_exceptions`), it only handles `__has_include` specially. All other `__` prefixed identifiers that aren't defined as macros don't push any value to the values stack, causing a "values stack is empty" error.

**Example failing code** (from `bits/c++config.h:233`):
```cpp
#if __cpp_exceptions
#  define _GLIBCXX_THROW_OR_ABORT(_EXC) (throw (_EXC))
#else
#  define _GLIBCXX_THROW_OR_ABORT(_EXC) (__builtin_abort())
#endif
```

**Fix Required**: In `evaluate_expression()`, after the `if (keyword.find("__has_include") == 0)` block, add an `else` clause that treats unknown `__` identifiers as undefined (pushing 0):

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

#### Issue 2: Missing C++ Feature Test Macros

**Problem**: The standard library headers rely heavily on C++ feature test macros to enable/disable features based on compiler/language support. These need to be predefined in the preprocessor.

**Essential macros to define**:

| Macro | Value | Description |
|-------|-------|-------------|
| `__cpp_exceptions` | 199711L | Exception support |
| `__cpp_rtti` | 199711L | RTTI support |
| `__cpp_noexcept_function_type` | 201510L | C++17 noexcept in function type |
| `__cpp_constexpr` | 202002L | C++20 constexpr |
| `__cpp_concepts` | 202002L | C++20 concepts |
| `__cpp_if_constexpr` | 201606L | C++17 if constexpr |
| `__cpp_inline_variables` | 201606L | C++17 inline variables |
| `__cpp_structured_bindings` | 201606L | C++17 structured bindings |
| `__cpp_variadic_templates` | 200704L | Variadic templates |
| `__cpp_static_assert` | 200410L | Static assert |
| `__cpp_decltype` | 200707L | decltype |
| `__cpp_range_based_for` | 200907L | Range-based for |
| `__cpp_lambdas` | 200907L | Lambda expressions |
| `__cpp_initializer_lists` | 200806L | Initializer lists |
| `__cpp_delegating_constructors` | 200604L | Delegating constructors |
| `__cpp_nullptr` | 200802L | nullptr |
| `__cpp_auto_type` | 200606L | auto type |

**Location to add**: `FileReader.h`, in `addBuiltinDefines()` method

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

1. **First Priority**: Fix the `evaluate_expression()` bug where `__` prefixed identifiers don't push a value. This is the immediate crash cause.

2. **Second Priority**: Add basic feature test macros (`__cpp_exceptions`, `__cpp_rtti`, `__cpp_constexpr`, etc.) to `addBuiltinDefines()`.

3. **Third Priority**: Implement `__has_feature`, `__has_builtin`, etc. as preprocessor intrinsics.

4. **Fourth Priority**: Add sanitizer macro handling.

### Test File Location

The diagnostic test case is located at:
- `tests/FlashCppTest/FlashCppTest/FlashCppTest/FlashCppTest.cpp` (TEST_CASE "StandardHeaders:DiagnosticReport")

### Additional Notes

- The test shell script `tests/test_standard_headers.sh` provides a comprehensive test of all major standard headers
- Once the preprocessor issues are fixed, additional parser and semantic issues may be discovered
- Standard library support is a significant undertaking that will require ongoing development

## Headers Tested

The following headers were tested (all currently fail at the preprocessor stage):

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
