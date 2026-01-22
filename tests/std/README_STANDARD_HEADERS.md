# Standard Header Tests

This directory contains test files for C++ standard library headers to assess FlashCpp's compatibility with the C++ standard library.

## Current Status

| Header | Test File | Status | Notes |
|--------|-----------|--------|-------|
| `<limits>` | `test_std_limits.cpp` | ✅ Compiled | ~0.21s |
| `<type_traits>` | `test_std_type_traits.cpp` | ✅ Compiled | ~1.1s release, ~6s debug |
| `<compare>` | N/A | ✅ Compiled | ~0.07s |
| `<version>` | N/A | ✅ Compiled | ~0.07s |
| `<source_location>` | N/A | ✅ Compiled | ~0.07s |
| `<numbers>` | N/A | ✅ Compiled | ~1.2s release |
| `<initializer_list>` | N/A | ✅ Compiled | ~0.04s |
| `<ratio>` | `test_std_ratio.cpp` | ❌ Codegen | 2026-01-21: Variable template in function body issue (see blocker 7.4) |
| `<vector>` | `test_std_vector.cpp` | ⏱️ Timeout | 2026-01-21 PM: Times out at 15s during template instantiation |
| `<tuple>` | `test_std_tuple.cpp` | ⏱️ Timeout | 2026-01-21 PM: Times out at 15s during template instantiation |
| `<optional>` | `test_std_optional.cpp` | ❌ Parse Error | 2026-01-21 PM: Function return type loss issue with `_Hash_bytes` (see blocker) |
| `<variant>` | `test_std_variant.cpp` | 💥 Crash | 2026-01-21 PM: Progresses past line 299 (base class fix), then crashes during codegen |
| `<any>` | `test_std_any.cpp` | ⏱️ Timeout | 2026-01-21: Times out at 60+ seconds (was misreported as parse error) |
| `<concepts>` | `test_std_concepts.cpp` | ⏱️ Timeout | Times out at 5+ minutes during template instantiation |
| `<utility>` | `test_std_utility.cpp` | ⏱️ Timeout | 2026-01-21 PM: Times out at 15s during template instantiation |
| `<bit>` | N/A | ⏱️ Timeout | Times out at 5+ minutes during template instantiation |
| `<string_view>` | `test_std_string_view.cpp` | ⏱️ Timeout | Times out at 60+ seconds |
| `<string>` | `test_std_string.cpp` | ⏱️ Timeout | Times out at 60+ seconds |
| `<array>` | `test_std_array.cpp` | ⏱️ Timeout | 2026-01-21 PM: Times out at 15s during template instantiation |
| `<memory>` | `test_std_memory.cpp` | ⏱️ Timeout | 2026-01-21 PM: Times out at 15s during template instantiation |
| `<functional>` | `test_std_functional.cpp` | ⏱️ Timeout | 2026-01-21 PM: Times out at 15s during template instantiation |
| `<algorithm>` | `test_std_algorithm.cpp` | ⏱️ Timeout | Times out at 60+ seconds |
| `<map>` | `test_std_map.cpp` | ⏱️ Timeout | 2026-01-21: Now progresses past typename funccast fix, times out |
| `<set>` | `test_std_set.cpp` | ⏱️ Timeout | 2026-01-21: Now progresses past typename funccast fix, times out |
| `<span>` | `test_std_span.cpp` | ⏱️ Timeout | 2026-01-21: Times out during template instantiation |
| `<ranges>` | `test_std_ranges.cpp` | ⏱️ Timeout | Times out at 60+ seconds |
| `<iostream>` | `test_std_iostream.cpp` | ❌ Codegen | 2026-01-21: MemberAccess missing object issue |
| `<chrono>` | `test_std_chrono.cpp` | ⏱️ Timeout | Times out at 60+ seconds |
| `<atomic>` | N/A | ❌ Parse Error | Complex decltype in partial specialization (see blockers) |
| `<new>` | N/A | ✅ Compiled | ~0.08s |
| `<exception>` | N/A | ✅ Compiled | ~6s (2026-01-21 PM: Fixed MemberAccess issue) |
| `<typeinfo>` | N/A | ✅ Compiled | ~0.09s |
| `<typeindex>` | N/A | ✅ Compiled | ~0.14s |
| `<csetjmp>` | N/A | ✅ Compiled | ~0.04s |
| `<csignal>` | N/A | ✅ Compiled | ~0.13s |
| `<stdfloat>` | N/A | ✅ Compiled | ~0.01s (C++23) |
| `<spanstream>` | N/A | ✅ Compiled | ~0.09s (C++23) |
| `<print>` | N/A | ✅ Compiled | ~0.09s (C++23) |
| `<expected>` | N/A | ✅ Compiled | ~0.08s (C++23) |
| `<text_encoding>` | N/A | ✅ Compiled | ~0.08s (C++26) |
| `<barrier>` | N/A | ✅ Compiled | ~0.07s (C++20) |
| `<stacktrace>` | N/A | ✅ Compiled | ~0.07s (C++23) |
| `<coroutine>` | N/A | ❌ Not Supported | Coroutines require `-fcoroutines` flag |

**Legend:** ✅ Compiled | ❌ Failed/Parse Error | ⏱️ Timeout (>30s)

**Note (2026-01-20):** Previous reports of "💥 Crash" for `<functional>`, `<algorithm>`, and `<chrono>` were actually timeouts. Investigation with enhanced exception logging confirmed no crashes occur - these headers timeout due to excessive template instantiation. See section 6.4 for details.

### C Library Wrappers (Also Working)

| Header | Test File | Notes |
|--------|-----------|-------|
| `<cstddef>` | `test_cstddef.cpp` | `size_t`, `ptrdiff_t`, `nullptr_t` (~0.13s) |
| `<cstdlib>` | `test_cstdlib.cpp` | `malloc`, `free`, etc. (~0.05s) |
| `<cstdio>` | `test_cstdio_puts.cpp` | `printf`, `puts`, etc. (~0.12s) |
| `<cstdint>` | N/A | `int32_t`, `uint64_t`, etc. (~0.04s) |
| `<cstring>` | N/A | `memcpy`, `strlen`, etc. (~0.12s) |
| `<ctime>` | N/A | `time_t`, `clock`, etc. (~0.08s) |
| `<climits>` | N/A | `INT_MAX`, `LONG_MAX`, etc. (~0.03s) |
| `<cfloat>` | N/A | `FLT_MAX`, `DBL_MIN`, etc. (~0.04s) |
| `<cassert>` | N/A | `assert` macro (~0.04s) |
| `<cerrno>` | N/A | `errno` (~0.03s) |
| `<clocale>` | N/A | `setlocale`, `localeconv` (~0.04s) |
| `<cstdarg>` | N/A | `va_list`, `va_start`, etc. (~0.03s) |
| `<cfenv>` | N/A | `fenv_t`, `fegetenv`, etc. (~0.03s) |
| `<cinttypes>` | N/A | `imaxabs`, `imaxdiv`, etc. (~0.04s) |
| `<cctype>` | N/A | `isalpha`, `isdigit`, etc. (~0.05s) |
| `<cuchar>` | N/A | `char16_t`, `char32_t` conversions (~0.13s) |
| `<cwchar>` | N/A | `wchar_t` functions (~0.56s) |
| `<cwctype>` | N/A | `iswupper`, `iswlower`, etc. (~0.78s) (2026-01-16: Fixed parenthesized identifier followed by `<`) |
| `<cstdbool>` | N/A | C99 `bool` compatibility (~0.13s) |
| `<cstdalign>` | N/A | C11 alignment specifiers (~0.13s) |
| `<ciso646>` | N/A | Alternative operator spellings (~0.03s) |

## Running the Tests

```bash
cd tests/std
./test_std_headers_comprehensive.sh
```

## Viewing Parser Progress

To see progress logging during compilation, build with Info level logging enabled:

```bash
# Build release with progress logging
clang++ -std=c++20 -DFLASHCPP_LOG_LEVEL=2 -O3 -I src -o x64/InfoRelease/FlashCpp \
    src/AstNodeTypes.cpp src/ChunkedAnyVector.cpp src/Parser.cpp \
    src/CodeViewDebug.cpp src/ExpressionSubstitutor.cpp src/main.cpp

# Run with progress output
./x64/InfoRelease/FlashCpp tests/std/test_std_type_traits.cpp
# Output: [Progress] 100 template instantiations in 1 ms (cache hit rate: 44.4%)
#         [Progress] 200 template instantiations in 4 ms (cache hit rate: 55.3%)
#         [Progress] Parsing complete: 7 top-level nodes, 58 AST nodes in 9 ms
```

### Template Profiling Statistics

For detailed template instantiation statistics, use the `--perf-stats` flag:

```bash
./x64/Release/FlashCpp test.cpp --perf-stats
# Shows:
#   - Template instantiation counts and timing
#   - Cache hit/miss rates
#   - Top 10 most instantiated templates
#   - Top 10 slowest templates
```

## Disabling Logging

Logging can be controlled at runtime and compile-time.

### Runtime Log Level Control

```bash
# Set global log level
./x64/Debug/FlashCpp file.cpp --log-level=warning

# Set log level for specific category
./x64/Debug/FlashCpp file.cpp --log-level=Parser:debug

# Available levels: error (0), warning (1), info (2), debug (3), trace (4)
# Available categories: General, Parser, Lexer, Templates, Symbols, Types, Codegen, Scope, Mangling, All
```

### Compile-time Log Level Control

```bash
# Build with specific log level (0=error, 1=warning, 2=info, 3=debug, 4=trace)
# Note: Default release build uses level 1 (warning only)
# Use level 2 to include Info messages (including progress logging)
clang++ -DFLASHCPP_LOG_LEVEL=2 -O3 ...
```

## Current Blockers

### 1. Function Argument Parsing in Member Functions (CRITICAL BLOCKER - 2026-01-21)

**Issue:** Function calls with member variable arguments inside member functions fail to parse correctly. FlashCpp reports finding the function overload but 0 arguments when it should find 1+ arguments.

**Error Message:**
```
/usr/include/c++/14/bits/nested_exception.h:79:18: error: No matching function for call to 'rethrow_exception'
  rethrow_exception(_M_ptr);
                   ^
```

**Debug output shows:**
```
[DEBUG][Parser] Function call to 'rethrow_exception': found 1 overload(s), 0 argument(s)
```

**Problematic Code Pattern:**
```cpp
class nested_exception {
    exception_ptr _M_ptr;
public:
    void rethrow_nested() const {
        rethrow_exception(_M_ptr);  // Fails here - argument not parsed
    }
};
```

**Investigation Findings:**
- The function `rethrow_exception` is found correctly (1 overload)
- The argument `_M_ptr` is identified and starts parsing
- In parse_expression's postfix operator loop (Parser.cpp:~16563), after consuming `_M_ptr`, the closing `)` of the function call is incorrectly consumed
- This causes the argument list to appear empty (0 arguments)
- Detailed log sequence:
  1. `consume_token: Consumed token='(', next token from lexer='_M_ptr'` - opens function call
  2. `>>> parse_expression: Starting with precedence=2, context=0, depth=2, current token: _M_ptr` - starts parsing argument
  3. `consume_token: Consumed token='_M_ptr', next token from lexer=')'` - consumes identifier
  4. `Postfix operator iteration 1: peek token type=7, value=')'` - sees closing paren
  5. `consume_token: Consumed token=')', next token from lexer=';'` - **INCORRECTLY CONSUMES closing paren**
  6. `Function call to 'rethrow_exception': found 1 overload(s), 0 argument(s)` - args vector is empty

**Root Cause:** The postfix operator loop in parse_expression doesn't properly stop when encountering `)` in a function argument context. The closing `)` should terminate argument parsing but is instead consumed as part of the argument expression.

**Test Cases:**
- Simple reproduction: `/tmp/test_rethrow_simple.cpp`
- Full header: `#include <exception>`

**Affected Headers:** 
- `<exception>` - directly affected at nested_exception.h:79
- `<optional>` - depends on `<exception>` 
- `<iostream>` - depends on `<exception>`
- Any header that uses exception handling

**Impact:** This is a **critical blocker** preventing compilation of most standard library headers that deal with exceptions. Must be fixed before significant progress can be made on exception-related headers.

**Potential Fix Direction:** The postfix operator loop needs to check if we're in a function argument context and avoid consuming `)` that belongs to the enclosing function call. This may require:
1. Adding context tracking to parse_expression
2. Checking expression context before entering postfix loop  
3. Or ensuring the loop exits before consuming delimiter tokens

### 2. Remaining Parse Blockers

#### 2.1 Context-Dependent Parse Error in `bits/utility.h` (2026-01-19)

**Note (2026-01-21):** This may be related to or masked by the function argument parsing issue (blocker #6). After fixing #6, this issue should be re-evaluated.

**Error Message:**
```
/usr/include/c++/14/bits/utility.h:139:49: error: Expected type after '=' in template parameter default
     typename _Up = typename remove_cv<_Tp>::type,
                                                  ^
```

**Problematic Code Pattern:**
```cpp
template<typename _Tp,
         typename _Up = typename remove_cv<_Tp>::type,  // Error reported here
         typename = typename enable_if<is_same<_Tp, _Up>::value>::type,
         size_t = tuple_size<_Tp>::value>
    using __enable_if_has_tuple_size = _Tp;
```

**Investigation Findings:**
- Simplified test cases with this exact pattern parse successfully in isolation
- The error only occurs after including `<type_traits>` and `<bits/move.h>`
- This suggests parser state pollution or context corruption from previous headers
- The parser fails to parse `typename remove_cv<_Tp>::type` correctly in this context
- `parse_type_specifier()` should stop at commas in template parameter lists, but appears to consume too much

**Test Cases:**
- `tests/test_template_alias_typename_default_ret0.cpp` - Works in isolation
- `tests/test_utility_with_bits_move_ret0.cpp` - Fails when including `<bits/move.h>`

**Affected Headers:** `<utility>`, `<tuple>`, `<span>`, `<array>`, and any header that depends on these

#### 2.2 Constructor and Member Functions with `noexcept = delete` in Partial Specializations (FIXED - 2026-01-21)

**Issue:** ~~The `<variant>` header fails with a parse error for constructors and member functions marked `noexcept = delete` in template partial specializations.~~ **RESOLVED**

**Previous Error Messages:**
```
/usr/include/c++/14/bits/enable_special_members.h:119:61: error: Expected type specifier
      operator=(_Enable_default_constructor const&) noexcept = default;
                                                              ^

/usr/include/c++/14/bits/enable_special_members.h:130:6: error: Expected type specifier
    { ~_Enable_destructor() noexcept = delete; };
       ^
```

**Problematic Code Patterns:**
```cpp
// In template partial specialization:
Type& operator=(const Type&) noexcept = default;
~Destructor() noexcept = delete;
```

**Root Cause:** Partial specialization body parsing didn't call `parse_function_trailing_specifiers()` to handle trailing specifiers (noexcept, override, final, = default, = delete) on member functions and destructors.

**Fix Applied:** 
1. Added `parse_function_trailing_specifiers()` call after parsing member function parameters in partial specializations (Parser.cpp ~line 27050)
2. Added destructor parsing support in partial specialization bodies with full trailing specifiers support (Parser.cpp ~line 27010)
3. Both defaulted and deleted functions/destructors are now properly handled

**Impact:** The `<variant>` header now progresses from line 119 (operator=) to line 72 (complex decltype in partial specialization pattern). This fix unblocks many headers that use `bits/enable_special_members.h`.

**Test Case:** Created `/tmp/test_operator_eq_template.cpp` which now compiles successfully

#### 2.3 Complex decltype in Partial Specialization Template Arguments (**FIXED** - 2026-01-21)

**Issue:** Partial specializations with complex decltype expressions containing nested template instantiations and function calls fail to parse.

**Status:** **FIXED** - Added handling in `parse_primary_expression` to recognize class templates followed by template args and `(` as functional-style cast (constructor call creating temporary object).

**Error Message (before fix):**
```
/usr/include/c++/14/bits/functional_hash.h:72:26: error: Expected template argument pattern in partial specialization
      struct __poison_hash<_Tp, __void_t<decltype(hash<_Tp>()(declval<_Tp>()))>>
                           ^
```

**Problematic Code Pattern:**
```cpp
template<typename _Tp, typename = void>
  struct __poison_hash { };

// Partial specialization with complex decltype:
template<typename _Tp>
  struct __poison_hash<_Tp, __void_t<decltype(hash<_Tp>()(declval<_Tp>()))>>
  { /* ... */ };
```

**Analysis:**
- The pattern involves: `decltype(hash<_Tp>()(declval<_Tp>()))`
- This is a decltype of a function call expression: `hash<_Tp>()(declval<_Tp>())`
- Which consists of: instantiate hash<_Tp>, construct an instance, call operator() with declval<_Tp>() as argument
- Parser needs to handle this complex nested expression as a template argument in partial specialization

**Investigation Update (2026-01-21):**
Root cause identified - the parser fails at step 2 (construct an instance) because:
1. After parsing `hash<_Tp>`, the parser sees `(`
2. `hash` is not recognized as a class template in this context, so `hash<_Tp>()` is not recognized as a functional-style cast / temporary object creation
3. Parser emits "Missing identifier: hash" error because it doesn't find `hash` as a function
4. This causes template argument parsing to fail with "Expected template argument pattern"

The fix requires: when parsing primary expressions in template contexts, after seeing `identifier<args>(`, recognize this as a functional-style cast (temporary object creation) if the identifier resolves to a class template, not just for function templates.

**Affected Headers:** `<variant>` (stops at line 72), potentially `<functional>`, `<optional>`, and others using hash-based SFINAE

**Key Finding:** Many patterns previously thought to be blockers actually **work correctly**:

✅ **Verified Working Patterns:**
- **Variadic non-type template params**: `template<size_t... _Indexes>` - Compiles successfully
  - Test case: `tests/test_variadic_nontype.cpp` 
- **Template alias with complex defaults**: Patterns like `typename _Up = typename remove_cv<_Tp>::type` work in isolation
  - Test case: `tests/test_utility_parse_error.cpp`

❌ **Actual Blockers:**
- **Logging Bug (FIXED 2026-01-21)**: Headers appeared to timeout not due to parse errors but due to logging bug where log arguments were evaluated even when filtered. With fix, headers compile in 8-11 seconds.
  - `<type_traits>` now compiles successfully
  - Complex headers like `<utility>`, `<functional>`, `<chrono>` still timeout due to template complexity (performance issue, not correctness)
- **Complex decltype in partial spec**: `__void_t<decltype(hash<T>()(...))>` still needs investigation
  - Test cases: `tests/test_just_type_traits.cpp` (was timing out, now works)
- **Context-dependent issues**: Parse errors occur only after including certain headers, suggesting parser state issues
  - Test case: `tests/test_utility_with_context.cpp`

#### 2.4 Variable Template Partial Specialization Pattern Matching (PARTIALLY FIXED - 2026-01-21)

**Previous Issue:** Variable template partial specializations like `__is_ratio_v<ratio<_Num, _Den>>` were registered without the base template name in the pattern, causing lookup failures.

**Fix Applied (Phase 1):** 
1. Store the base type name in `dependent_name` field when parsing partial specialization patterns
2. Check if `dependent_name` refers to a known template when building pattern key
3. Include template name in pattern (e.g., `__is_ratio_v_ratio`) only for template instantiation patterns

**Fix Applied (Phase 2 - 2026-01-21 PM):**
1. Extended `TemplateParamSubstitution` to include type parameter mappings (not just non-type values)
2. Register type substitutions during function template body re-parsing
3. Added substitution lookup in `try_instantiate_variable_template` to resolve template parameters
4. Enables `__is_ratio_v<_R1>` inside function templates to correctly substitute `_R1` with concrete types

**Example that NOW works:**
```cpp
template<typename _Tp>
constexpr bool __is_ratio_v = false;

template<long _Num, long _Den>
constexpr bool __is_ratio_v<ratio<_Num, _Den>> = true;

// Direct use works:
static_assert(__is_ratio_v<ratio<1,2>> == true);  // ✅ Works

// Simple function template returning variable template value:
template<typename _R>
constexpr bool is_ratio_check() { return __is_ratio_v<_R>; }  // ✅ Works
```

**Remaining Issue:** The `<ratio>` header still crashes during codegen because:
1. `if constexpr` evaluation with variable templates isn't fully working
2. The function `__are_both_ratios` uses nested `if constexpr` statements
3. The variable template identifier `__is_ratio_v` is not found in symbol table during code generation

**Affected Headers:** `<ratio>` (crashes during codegen in `__are_both_ratios`)

#### 2.4 Base Class Namespace Resolution (ACTIVE BLOCKER - 2026-01-21)

**Issue:** After fixing blocker #7.3, `<variant>` header progresses past line 72 but fails at line 299 with "Base class 'std' not found".

**Error Message:**
```
/usr/include/c++/14/bits/functional_hash.h:299:58: error: Base class 'std' not found
      struct __is_fast_hash<hash<long double>> : public std::false_type
                                                           ^
```

**Analysis:** The base class `std::false_type` is not being resolved correctly. This appears to be a namespace resolution issue where `std` is not recognized as a namespace in the base class specification.

**Affected Headers:** `<variant>`, potentially `<functional>`, `<optional>` and others that use `std::true_type` / `std::false_type` as base classes.

### 3. Template Instantiation Performance

Template-heavy headers (`<concepts>`, `<bit>`, `<string>`, `<ranges>`) time out due to instantiation volume. Key optimization: improve template cache hit rate (currently ~26%).
- Implement lazy instantiation for static members and whole template classes (see `docs/LAZY_TEMPLATE_INSTANTIATION_PLAN.md`)
- Optimize string operations in template name generation

### 4. Template Instantiation Performance

Most headers timeout due to template instantiation volume, not parsing errors. Individual instantiations are fast (20-50μs), but standard headers trigger thousands of instantiations.

**Optimization opportunities:**
- Improve template cache hit rate (currently ~26%)
- Optimize string operations in template name generation
- Consider lazy evaluation strategies

### 5. std::initializer_list Compiler Magic (Known Limitation)

**Issue:** `std::initializer_list<T>` requires special compiler support that is not yet implemented.

**Example that fails:**
```cpp
#include <initializer_list>
int main() {
    std::initializer_list<int> list = {1, 2, 3};  // Error: Too many initializers
    return 0;
}
```

**Using initializer_list as constructor argument also fails:**
```cpp
#include <initializer_list>

class Container {
public:
    int sum;
    Container(std::initializer_list<int> list) : sum(0) {
        for (const int* it = list.begin(); it != list.end(); ++it) {
            sum += *it;
        }
    }
};

int main() {
    Container c{1, 2, 3};  // Error: "Too many initializers for struct"
    return c.sum;
}
```

**Root cause:** When parsing `Container c{1, 2, 3}`, FlashCpp attempts to match the 3 integer arguments to constructor parameters, but Container only has one constructor taking `std::initializer_list<int>`. The compiler magic needed would:
1. Detect that Container has an initializer_list constructor
2. Create a temporary array on the stack
3. Construct an `std::initializer_list` pointing to that array
4. Call the constructor with that initializer_list

**Expected behavior in standard C++:**
1. The compiler creates a temporary array `int __temp[] = {1, 2, 3}` on the stack
2. The compiler constructs `std::initializer_list` using its private constructor with a pointer to the array and size 3
3. This is compiler magic - the private constructor is only accessible to the compiler

**Current behavior in FlashCpp:**
- FlashCpp treats `std::initializer_list` like any other struct and tries aggregate initialization
- Since `std::initializer_list` only has 2 members (`_M_array` pointer and `_M_len` size), `{1, 2, 3}` fails with "too many initializers"
- For constructor calls, FlashCpp tries to match braced values to constructor parameters directly

**Workaround:** Use the default constructor and don't rely on brace-enclosed initializer lists for `std::initializer_list`:
```cpp
std::initializer_list<int> empty_list;  // Works - default constructor
std::initializer_list<int> two = {ptr, 2};  // Works - matches member count
```

**Note:** This is a fundamental limitation that affects many standard library patterns like range-based for loops with initializer lists (`for (int x : {1, 2, 3})`), container construction (`std::vector<int> v{1,2,3}`), and any class with an initializer_list constructor.

## Adding New Standard Header Tests

1. Create `test_std_<header>.cpp`:
   ```cpp
   #include <header>
   int main() { return 0; }
   ```

2. Verify valid C++20: `clang++ -std=c++20 -c test_std_<header>.cpp`

3. Test: `./test_std_headers_comprehensive.sh`

4. If it fails, add to `EXPECTED_FAIL` in `../run_all_tests.sh`

## See Also

- [`STANDARD_HEADERS_MISSING_FEATURES.md`](./STANDARD_HEADERS_MISSING_FEATURES.md) - Detailed analysis of missing features and implementation history
