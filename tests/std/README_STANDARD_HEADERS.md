# Standard Header Tests

This directory contains test files for C++ standard library headers to assess FlashCpp's compatibility with the C++ standard library.

## Current Status

✅ **Variable Template Partial Specialization Pattern Matching (2026-01-21):** Fixed pattern registration for partial specializations like `__is_ratio_v<ratio<_Num, _Den>>`. Now correctly includes base template name in pattern (e.g., `__is_ratio_v_ratio`). This enables variable template partial specializations with template instantiation patterns to work correctly when used directly.

🔧 **Variable Template in Function Template Body (2026-01-21):** Variable templates with partial specializations used inside function template bodies (like `if constexpr (__is_ratio_v<_R1>)` inside `__are_both_ratios<R1, R2>()`) don't properly substitute template parameters. The variable template is instantiated with the template parameter name (`_R1`) instead of the substituted type. This affects `<ratio>` header's `__are_both_ratios()` function.

✅ **Trailing Specifiers in Partial Specializations (2026-01-21 PM):** Fixed parsing of `operator=(...) noexcept = default` and `~Destructor() noexcept = delete` in partial specializations. `<variant>` progresses from line 119 → 72.

🔧 **MemberAccess Missing Object in Codegen (2026-01-21):** `<exception>` header crashes during code generation with "MemberAccess missing object: other". This appears to be related to accessing members of reference parameters in certain contexts. Blocks `<exception>`, `<optional>`, `<iostream>`.

✅ **Typename Functional Cast (2026-01-21):** Fixed `typename T<Args>::member(args)` pattern in fold expressions. Unblocks `<vector>`, `<map>`, `<set>` from parse errors.

✅ **Constexpr Constructor in Partial Specialization (2026-01-21):** Fixed `constexpr _Enable_default_constructor() noexcept = delete` pattern in partial specializations.

✅ **Logging Bug (2026-01-21):** Fixed FLASH_LOG macros evaluating arguments even when filtered. Debug builds now 8-11s instead of 60+s.

✅ **Investigation Update (2026-01-20):** Parser features mostly complete. Real blocker was logging performance. `<functional>`, `<algorithm>`, `<chrono>` compile without logging.

✅ **static_assert with Template-Dependent Expressions (2026-01-20):** Fixed constant expression evaluation for fold expressions and variable templates in static_assert.

✅ **Silent Failure Investigation (2026-01-19):** Added error tracing. Headers now properly display parse errors instead of silently failing.

✅ **Log Level Bug (2026-01-18):** Fixed release builds hang bug. All log levels work correctly.

✅ **Comprehensive Header Audit (2026-01-19):** Re-tested all headers with extended timeouts. Many "timeout" headers actually have specific parse errors.

| Header | Test File | Status | Notes |
|--------|-----------|--------|-------|
| `<limits>` | `test_std_limits.cpp` | ✅ Compiled | ~0.21s |
| `<type_traits>` | `test_std_type_traits.cpp` | ✅ Compiled | ~1.1s release, ~6s debug |
| `<compare>` | N/A | ✅ Compiled | ~0.07s |
| `<version>` | N/A | ✅ Compiled | ~0.07s |
| `<source_location>` | N/A | ✅ Compiled | ~0.07s |
| `<numbers>` | N/A | ✅ Compiled | ~1.2s release |
| `<initializer_list>` | N/A | ✅ Compiled | ~0.04s |
| `<ratio>` | `test_std_ratio.cpp` | ❌ Codegen | 2026-01-21: Variable template in function body issue (see blocker #8) |
| `<vector>` | `test_std_vector.cpp` | ⏱️ Timeout | 2026-01-21 PM: Times out at 15s during template instantiation |
| `<tuple>` | `test_std_tuple.cpp` | ⏱️ Timeout | 2026-01-21 PM: Times out at 15s during template instantiation |
| `<optional>` | `test_std_optional.cpp` | ❌ Codegen | 2026-01-21: MemberAccess missing object issue (see blocker #9) |
| `<variant>` | `test_std_variant.cpp` | ❌ Parse Error | 2026-01-21 PM: IMPROVED from line 119 to line 72 - decltype in partial spec pattern (~5.7s) |
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
| `<exception>` | N/A | ❌ Codegen | 2026-01-21: MemberAccess missing object during codegen |
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

### 1. Log Level Bug (FIXED - 2026-01-18)

The `if constexpr (enabled)` blocks in logging macros previously caused hangs when compiled out. Fixed by replacing with preprocessor `#if` checks (commit 6ea920f).

**Result:** `<type_traits>` now compiles successfully in ~6s.

### 2. Qualified Template Aliases in Template Arguments (FIXED - 2026-01-19)

**Issue:** ~~Template aliases qualified with a namespace (e.g., `__detail::__cref<_Lhs>`) are not recognized as templates when used inside template argument lists.~~ **RESOLVED**

**Previous error:**
```
/usr/include/c++/14/concepts:227:55: error: Expected ';' after concept definition
        && common_reference_with<__detail::__cref<_Lhs>, __detail::__cref<_Rhs>>
                                                        ^
```

**Root cause (now fixed):** When parsing `common_reference_with<__detail::__cref<_Lhs>, ...>`, the parser reached `__detail::__cref` and saw the `<` after it. In template argument context, it checked if `__cref` was a known template, but failed to find it because:
1. The parser only checked for class templates and variable templates, not alias templates
2. Alias templates in namespaces were only registered with simple names, not namespace-qualified names

**Fix applied (commit e45648b):**
1. Added alias template lookup (`lookup_alias_template`) in three parser code paths that determine if `<` should be parsed as template arguments
2. Fixed namespace-level alias template registration to also register with namespace-qualified names (like class templates do)
3. Added alias template resolution in expression parsing: when a qualified alias template like `detail::cref<int>` is parsed, it now resolves to the underlying type (`int`)

**Test case:** `tests/test_qualified_template_alias_ret0.cpp`

**Impact:** Patterns like `SomeTemplate<namespace::alias<ConcreteType>>` now work correctly. The `<concepts>` header still times out due to template instantiation volume, but the parsing phase now completes successfully.

### 3. `<ratio>` Header (FIXED - 2026-01-19)

Two crashes were fixed:
1. Skip deferred base class instantiation when template arguments can't be fully resolved
2. Guard `handleGlobalLoad` against being called outside function context

**Current Status:** The `<ratio>` header now **compiles successfully** in ~1.4 seconds!

### 4. Enum Class Forward Declaration (FIXED - 2026-01-19)

Added forward declaration support: `enum class byte : unsigned char;` now parses correctly.

### 5. GCC Extensions Support (FIXED - 2026-01-19)

- **`__typeof__`**: Now works like `decltype` for GCC compatibility
- **Using-declarations in structs**: `using Base::member;` now correctly imports member names

### 6. Function Argument Parsing in Member Functions (CRITICAL BLOCKER - 2026-01-21)

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

### 7. Remaining Parse Blockers

#### 7.1 Context-Dependent Parse Error in `bits/utility.h` (2026-01-19)

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

#### 7.2 Constructor and Member Functions with `noexcept = delete` in Partial Specializations (FIXED - 2026-01-21)

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

#### 7.3 Complex decltype in Partial Specialization Template Arguments (ACTIVE BLOCKER - 2026-01-21)

**Issue:** Partial specializations with complex decltype expressions containing nested template instantiations and function calls fail to parse.

**Error Message:**
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

#### 7.4 Variable Template Partial Specialization Pattern Matching (PARTIALLY FIXED - 2026-01-21)

**Previous Issue:** Variable template partial specializations like `__is_ratio_v<ratio<_Num, _Den>>` were registered without the base template name in the pattern, causing lookup failures.

**Fix Applied:** 
1. Store the base type name in `dependent_name` field when parsing partial specialization patterns
2. Check if `dependent_name` refers to a known template when building pattern key
3. Include template name in pattern (e.g., `__is_ratio_v_ratio`) only for template instantiation patterns

**Remaining Issue:** When variable templates with partial specializations are used inside function template bodies, the template parameter substitution doesn't propagate to nested variable template instantiations.

**Example that NOW works:**
```cpp
template<typename _Tp>
constexpr bool __is_ratio_v = false;

template<long _Num, long _Den>
constexpr bool __is_ratio_v<ratio<_Num, _Den>> = true;

// Direct use works:
static_assert(__is_ratio_v<ratio<1,2>> == true);  // ✅ Works
```

**Example that STILL fails:**
```cpp
template<typename _R1, typename _R2>
constexpr bool __are_both_ratios() {
    if constexpr (__is_ratio_v<_R1>)  // ❌ _R1 not substituted
        return true;
    return false;
}
// Fails because _R1 is not substituted before variable template lookup
```

**Affected Headers:** `<ratio>` (crashes during codegen in `__are_both_ratios`)

#### 7.5 MemberAccess Missing Object in Code Generation (ACTIVE BLOCKER - 2026-01-21)

**Issue:** The `<exception>` header crashes during code generation with "MemberAccess missing object: other".

**Error Message:**
```
[ERROR][Codegen] MemberAccess missing object: other
FlashCpp: src/IRConverter.h:13112: Assertion failed: "Struct object not found in scope or globals"
```

**Analysis:**
- The error occurs when accessing a member of a reference parameter named `other`
- Simple test cases with `operator=(const Type& other)` work correctly
- The issue appears in more complex contexts in the standard library
- May be related to how reference parameters are handled in certain struct/class contexts

**Test Results:**
- Simple member access: ✅ Works
- `<exception>` header: ❌ Crashes

**Affected Headers:** `<exception>`, `<optional>`, `<iostream>` (all depend on exception handling)

#### 7.5 _Hash_bytes Function Lookup Failure (ACTIVE BLOCKER - 2026-01-21)

**Issue:** Calls to `std::_Hash_bytes` fail with "No matching function" error when called from member functions.

**Error Message:**
```
/usr/include/c++/14/typeinfo:122:25: error: No matching function for call to '_Hash_bytes'
        return _Hash_bytes(name(), __builtin_strlen(name()), static_cast<size_t>(0xc70f6907UL));
                          ^
```

**Problematic Code Pattern:**
```cpp
namespace std {
  size_t _Hash_bytes(const void* __ptr, size_t __len, size_t __seed);
  
  class type_info {
    size_t hash_code() const {
      return _Hash_bytes(name(), __builtin_strlen(name()), 
                        static_cast<size_t>(0xc70f6907UL));  // Lookup fails here
    }
  };
}
```

**Analysis:**
- Function `_Hash_bytes` is declared in `std` namespace
- Call is made from within a member function of `std::type_info`
- The function takes 3 arguments and should be found via namespace lookup
- May be related to ADL (Argument-Dependent Lookup) or namespace visibility in member function context

**Affected Headers:** `<optional>` (stops at ~3.7s), `<iostream>`, and any header depending on `<exception>` → `<typeinfo>`

### 8. Template Instantiation Performance

Template-heavy headers (`<concepts>`, `<bit>`, `<string>`, `<ranges>`) time out due to instantiation volume. Key optimization: improve template cache hit rate (currently ~26%).
- Implement lazy instantiation for static members and whole template classes (see `docs/LAZY_TEMPLATE_INSTANTIATION_PLAN.md`)
- Optimize string operations in template name generation

### 5. Variable Templates in Type Context (FIXED - 2026-01-14)

**Issue:** ~~Variable templates used as non-type arguments in class template contexts were causing "No primary template found" errors.~~ **RESOLVED**

**Example pattern:**
```cpp
// Variable template like std::is_reference_v
template<typename _Tp>
inline constexpr bool is_reference_v = false;

// Used as non-type argument in class template:
template<typename _Xp, typename _Yp>
struct common_ref_impl : enable_if<is_reference_v<condres_cvref<_Xp>>, condres_cvref<_Xp>> {};
```

**Previous error:** `No primary template found for 'is_reference_v'`

**Root cause:** When parsing `is_reference_v<T>` in a type context (as an argument to `enable_if`), the parser was calling `try_instantiate_class_template()` without first checking if the identifier was a variable template. Variable templates are expressions, not types.

**Fix applied:** Added a check in `parse_type_specifier()` (Parser.cpp) to skip class template instantiation if the identifier is a variable template. The check uses `gTemplateRegistry.lookupVariableTemplate()` with both unqualified and namespace-qualified lookups.

**Test case:** `tests/test_variable_template_in_enable_if_ret0.cpp`

**Previous blockers resolved (January 15, 2026):**
- Namespace class member function call mangling: Member function calls on classes defined in namespaces now link correctly
  - **Issue:** When calling `t.get_value()` where `t` is of type `ns::Test`, the function call was mangled as `_ZN4Test9get_valueEv` (missing namespace) while the function was defined as `_ZN2ns4Test9get_valueEv` (with namespace), causing linker errors
  - **Root cause:** The StructTypeInfo was created with the simple name (`Test`) instead of the namespace-qualified name (`ns::Test`), causing mismatched mangling between function definitions and calls
  - **Fix:** 
    1. Modified Parser.cpp `parse_struct_declaration()` to use namespace-qualified names for TypeInfo and StructTypeInfo creation
    2. Modified CodeGen.h to use parent_struct_name directly instead of looking up TypeInfo's name (which could cause double-namespace in mangling)
  - **Test case:** `tests/test_namespace_class_member_call_ret42.cpp`
- Less-than vs template argument disambiguation: Pattern `integral_constant<bool, _R1::num < _R2::num>` now works
  - **Issue:** When parsing template arguments, `<` was incorrectly interpreted as starting template arguments instead of as a comparison operator in patterns like `_R1::num < _R2::num>`
  - **Root cause:** Multiple code paths would see `<` after a qualified identifier like `_R1::num` and immediately try to parse template arguments without checking if `num` was actually a template
  - **Fix:** Added checks in 6 code paths to verify if the member is a known template before parsing `<` as template arguments:
    1. `try_parse_member_template_function_call()` - member template function call parsing
    2. `parse_qualified_identifier_after_template()` - added `template` keyword tracking to honor explicit `::template` syntax
    3. `parse_type_specifier()` qualified name handling - check if base is template param and member is not a template  
    4. `parse_type_specifier()` dependent template handling - check template registry before parsing template args
    5. `parse_expression()` binary operator loop - check for QualifiedIdentifierNode/MemberAccessNode and verify template status
    6. Qualified identifier template argument parsing in expression context - check if member is known template
  - **Test case:** `tests/test_less_in_base_class_ret0.cpp`
- noexcept(expr) as template argument: Pattern `bool_constant<noexcept(declval<T&>().~T())>` now works
  - **Fix:** Added handling in `parse_explicit_template_arguments()` to accept NoexceptExprNode, SizeofExprNode, AlignofExprNode, and TypeTraitExprNode as dependent template arguments when constant evaluation fails
  - **Test case:** `tests/test_noexcept_template_arg_ret0.cpp`
- Static const member visibility in static_assert: Pattern `static_assert(value == 42, "msg");` within struct now works
  - **Fix:** Added early lookup for static members in `parse_expression()` and passed struct context to ConstExprEvaluator
  - **Test case:** `tests/test_static_assert_member_visibility_ret0.cpp`
- String literal concatenation in static_assert: Multi-line string messages now work
  - **Fix:** Modified `parse_static_assert()` to consume multiple adjacent string literals
  - **Test case:** `tests/test_static_assert_string_concat_ret0.cpp`
- Added `__INTMAX_MAX__`, `__INTMAX_MIN__`, `__UINTMAX_MAX__` predefined macros required by `<ratio>` header

**Previous blockers resolved (January 14, 2026):**
- Variable templates in type context: Pattern `enable_if<is_reference_v<T>, U>` where `is_reference_v` is a variable template
  - **Fix:** Added variable template check in `parse_type_specifier()` before calling `try_instantiate_class_template()`
  - **Test case:** `tests/test_variable_template_in_enable_if_ret0.cpp`
- Template member constructors: Pattern `template<typename U> Box(const Box<U>& other)` now parses correctly
  - **Fix:** Added template constructor detection in `parse_member_function_template()` before calling `parse_template_function_declaration_body()`
  - **Test case:** `tests/test_template_ctor_ret0.cpp`
- Namespace-qualified variable template lookup: Variable templates in namespaces can now be used from function templates in the same namespace
  - **Fix:** Added namespace-qualified lookup in multiple code paths for variable templates in `parse_unary_expression()`
  - **Test case:** `tests/test_ns_var_template_ret0.cpp`
- Non-type template parameters in return types: Pattern `typename tuple_element<_Int, pair<_Tp1, _Tp2>>::type&` now works
  - **Fix:** Set `current_template_param_names_` EARLY in `parse_template_declaration()`, before variable template detection code calls `parse_type_specifier()`
  - **Test case:** `tests/test_nontype_template_param_return_ret0.cpp`

**Previous blockers resolved (January 13, 2026):**
- Member template function calls: Pattern `Helper<int>::Check<int>()` now works
- Template friend declarations: Pattern `template<typename _U1, typename _U2> friend struct pair;` now works
- Variable template brace initialization: Pattern `inline constexpr in_place_type_t<_Tp> in_place_type{};` now works
- C++17 nested namespaces: Pattern `namespace A::B::C { }` now works
- C++20 inline nested namespaces: Pattern `namespace A::inline B { }` now works
- `const typename` in type specifiers: Pattern `constexpr const typename T::type` now works

### 3. Template Argument Reference Preservation (FIXED - 2026-01-14)

**Issue:** ~~Template argument substitution can lose reference qualifiers when substituting type parameters.~~ **RESOLVED**

**Example pattern:**
```cpp
template<typename T>
constexpr bool test() {
    return is_reference_v<T>;  // T=int& now correctly preserves reference
}

test<int&>();  // Now correctly returns true
test<int&&>(); // Now correctly returns true
```

**Fix applied:** Updated three code paths in `Parser.cpp` and `TemplateRegistry.h`:
1. `toTemplateArgument()`: Check `is_rvalue_reference` BEFORE `is_reference` since both flags are true for rvalue references
2. `try_instantiate_template_explicit()`: Use `toTemplateArgument()` to preserve full type info including references instead of just `makeType(base_type)`
3. `mangleTemplateName()`: Include reference qualifiers (`R` for `&`, `RR` for `&&`) in mangled names to generate distinct instantiations
4. Template parameter registration: Preserve `is_reference_` and `is_rvalue_reference_` in `TypeInfo` when setting up type aliases during template body re-parsing

**Test case:** `tests/test_template_ref_preservation_ret0.cpp`

### 4. Type Alias as Base Class (FIXED - 2026-01-15)

**Issue:** ~~Type aliases (using declarations) cannot currently be used as base classes when qualified with a namespace.~~ **RESOLVED**

**Example that now works:**
```cpp
namespace std {
    template<typename T, T v>
    struct integral_constant { static constexpr T value = v; };
    
    using false_type = integral_constant<bool, false>;
}

struct Test : std::false_type {};  // Now works!
```

**Fix applied:** Two changes in `Parser.cpp`:
1. Type aliases in namespaces now register with namespace-qualified names in `gTypesByName`
2. `validate_and_add_base_class()` now resolves type aliases by following `type_index_` chain to find the underlying struct type

**Test case:** `tests/test_type_alias_base_class_ret0.cpp`

### 5. Template Instantiation Performance

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

### 6. Missing Infrastructure

- **Exception handling** - Required for containers (`<vector>`, `<string>`)
- **Allocator support** - Required for `<vector>`, `<string>`, `<map>`, `<set>`
- **Locales** - Required for `<iostream>`

## Adding New Standard Header Tests

1. Create `test_std_<header>.cpp`:
   ```cpp
   #include <header>
   int main() { return 0; }
   ```

2. Verify valid C++20: `clang++ -std=c++20 -c test_std_<header>.cpp`

3. Test: `./test_std_headers_comprehensive.sh`

4. If it fails, add to `EXPECTED_FAIL` in `../run_all_tests.sh`

## Implemented Features Summary

The following features have been implemented to support standard headers:

**Preprocessor:**
- Multiline macro invocations (macro arguments spanning multiple lines)
- Angle bracket protection in variadic macro arguments (commas inside `<>` are preserved)

**Type System:**
- Type traits intrinsics (`__is_same`, `__is_class`, `__is_pod`, etc.)
- Library type traits vs. intrinsic disambiguation (`__is_swappable<T>` vs `__is_void(T)`)
- Conversion operators (`operator T()`)
- Function pointer typedefs
- Variable templates with partial specializations
- Variable templates with brace initialization (`constexpr Type<T> name{};`)
- Function reference/pointer types in template arguments
- Function pointer parameters with pack expansion (`void (*)(Args...)`) (NEW)
- Function pointer parameters with noexcept specifier (`void (*)() noexcept`) (NEW)
- Unnamed function pointer parameters (`void (*)()`) (NEW)

**Templates:**
- Fold expression evaluation in static members
- Namespace-qualified variable templates
- Namespace-qualified variable template lookup in function templates
- Member template requires clauses
- Template function `= delete`/`= default`
- Template friend declarations (`template<typename T1, typename T2> friend struct pair;`)
- Non-type template parameters in return types (`typename tuple_element<_Int, pair<_Tp1, _Tp2>>::type&`)
- Template member constructors (`template<typename U> Box(const Box<U>& other)`) (NEW)
- Template alias declarations with requires clauses (NEW)
- Brace initialization for instantiated template structs (NEW)

**C++17/C++20 Features:**
- C++17 nested namespace declarations (`namespace A::B::C { }`)
- C++20 inline nested namespace declarations (`namespace A::inline B { }`)
- Compound requirement noexcept specifier
- Template parameter brace initialization
- Globally qualified `::new`/`::delete`
- Template alias declarations with requires clauses (`template<typename T> requires Constraint<T> using Alias = T;`)
- Template argument reference preservation in function template instantiation
- ~~Coroutine support macro (`__cpp_impl_coroutine`) for `<coroutine>` header~~ (REMOVED - coroutines not supported)

**Other:**
- Named anonymous unions in typedef structs
- Direct initialization with `*this`
- Global scope `operator new`/`operator delete`
- Typedef array syntax (`typedef type name[size];`)
- Function pointer parameters with pack expansion and noexcept (NEW)
- Pointer-to-void implicit conversion in overload resolution (NEW)
- `__builtin_strlen` builtin function support (NEW)
- UserDefined type alias resolution in overload resolution (NEW)
- Friend function declarations with noexcept specifier (NEW)
- Friend operator function declarations (NEW)
- Friend function definitions with inline body (NEW)
- Out-of-line constructor/destructor definitions (NEW)
- Elaborated type specifiers with qualified names (NEW)
- Graceful handling of non-standard member sizes in code generation (NEW)
- Out-of-line operator definitions (`ReturnType ClassName::operator=(...)`) (NEW)
- Out-of-line member functions with different parameter names between declaration and definition (NEW)
- Pointer-to-member typedef syntax (`typedef T Class::* alias;`) (NEW)
- Trailing return type parameter visibility in decltype expressions (NEW)
- Namespace-qualified template alias resolution (`namespace::alias<T>` as template argument) (NEW)

## Recent Changes

Changes are listed in reverse chronological order.

### 2026-01-21 (Variable Template Partial Specialization Pattern Matching)
- **Fixed partial specialization pattern registration:** Partial specializations like `__is_ratio_v<ratio<_Num, _Den>>` now correctly include the base template name in the pattern key
  - Store base type name in `dependent_name` field when parsing partial specialization patterns
  - Check if `dependent_name` refers to a known template when building pattern key
  - Pattern now includes template name (e.g., `__is_ratio_v_ratio`) for template instantiation patterns
  - Simple dependent types like `T&` still use minimal pattern (e.g., `is_reference_v_R`)
  - Test case: `static_assert(__is_ratio_v<ratio<1,2>> == true)` now works for direct usage
- **Test Results:**
  - ✅ All 949 existing tests pass
  - ✅ Variable template partial specializations with template instantiation patterns work
  - ⚠️ Variable templates used inside function template bodies still don't substitute properly (see blocker 7.4)

**Remaining Issue:** When `__is_ratio_v<_R1>` is used inside a function template body like `__are_both_ratios<R1,R2>()`, the template parameter `_R1` is not substituted before variable template lookup. This requires deeper integration with function template instantiation.

**Affected:** `<ratio>` still crashes due to `__are_both_ratios()` function using variable templates internally.

### 2026-01-20 (static_assert with Template-Dependent Expressions - PR #XXX)
- **Fixed fold expression evaluation in static_assert:** Fold expressions like `(args && ...)` are now correctly treated as template-dependent
  - Added `FoldExpressionNode` handling in `ConstExprEvaluator` to return `TemplateDependentExpression` error type
  - Allows static assertions with fold expressions to be deferred until template instantiation
  - Test case: `template<typename... Args> struct Test { static_assert((true && ...), "test"); };` now compiles
  - **Impact:** Essential for many C++ standard library headers that use fold expressions in static assertions
- **Fixed pack expansion evaluation:** Pack expansions like `args...` are now treated as template-dependent
  - Added `PackExpansionExprNode` handling in `ConstExprEvaluator`
  - Prevents premature evaluation of pack expansions during template definition parsing
- **Fixed variable template handling:** Variable templates like `is_integral_v<_Tp>` with template parameters now work correctly
  - Added check for `TemplateVariableDeclarationNode` in `evaluate_identifier()` to defer evaluation
  - Prevents errors when variable templates appear in static_assert conditions with template parameters
  - Test case: `template<typename T> struct Test { static_assert(is_integral_v<T>); };` now compiles
- **Fixed qualified identifier with dependent template arguments:** Patterns like `__static_abs<_Pn>::value` now work
  - Enhanced `evaluate_qualified_identifier()` to detect template instantiations with dependent arguments
  - Checks if namespace part contains template argument separators and marks as template-dependent
  - Prevents "Undefined qualified identifier" errors for valid template-dependent code
- **Optimized string handling:** Refactored to use `string_view` to avoid unnecessary string copies
  - Works directly with namespace handle and name components instead of calling `full_name()`
  - Ensures `string_view` doesn't reference temporaries while improving performance
- **Test Results:**
  - ✅ All 945 existing tests pass
  - ✅ `<limits>` continues to compile successfully
  - ✅ `<type_traits>` continues to compile successfully
  - ⚠️ `<ratio>`, `<utility>`, `<tuple>`, `<variant>` still have parse/timeout issues (see blockers below)

**Commits:** c6c20eb, 3c555fd, 575a839

**Next Steps:**
1. **Context-Dependent Parse Errors** (High Priority): Investigate and fix the parse errors in `bits/utility.h` that affect `<utility>`, `<tuple>`, `<span>`, `<array>`
   - Root cause: Parser state pollution when certain headers are included before others
   - Pattern: `typename _Up = typename remove_cv<_Tp>::type` fails only in specific contexts
2. **Constructor with `noexcept = delete`** (Medium Priority): Fix parse error for constructors marked `noexcept = delete`
   - Affects: `<variant>` and potentially other headers using `bits/enable_special_members.h`
3. **Template Instantiation Performance** (Long-term): Optimize template instantiation to prevent timeouts
   - Headers like `<concepts>`, `<bit>`, `<string>`, `<ranges>` time out due to instantiation volume
   - Current cache hit rate: ~26% - needs improvement
   - Consider implementing lazy instantiation for static members and template classes

### 2026-01-19 (Silent Failure Investigation & Error Tracing)
- **Added error tracing infrastructure:** Parse errors are now always visible, even in release builds
  - Enhanced `main.cpp` to output parse errors to stderr in addition to logging system
  - Added catch-all exception handlers around main logic to catch uncaught exceptions
  - Previously silent failures (exit code 1 with no output) now show proper error messages
- **Discovered context-dependent parse errors:** Several headers fail with parse errors only when other headers are included first
  - `<utility>`, `<tuple>`, `<span>`, `<array>`: Fail with "Expected type after '=' in template parameter default" in `bits/utility.h:139`
  - `<variant>`: Fails with "Expected identifier token" for constructor with `noexcept = delete`
  - All simplified test cases pass in isolation, suggesting parser state corruption
- **Regression identified:** Headers previously marked as compiling (`<tuple>`, `<variant>`) now fail due to context-dependent issues
  - This may be related to recent parser changes or standard library version differences
  - Investigation ongoing to identify root cause

### 2026-01-19 (QualifiedIdentifierNode Fix - Some Headers Compile!)
- **Fixed nested template expressions in template parameter defaults:**
  - Added `QualifiedIdentifierNode` to the list of accepted dependent compile-time expressions
  - This fixes patterns like `template<typename T, typename X = enable_if<is_same<T, int>::value>>`
  - The `is_same<T, int>::value` expression was previously failing to be accepted as a dependent template argument
- **Status Update:** Some headers now have context-dependent failures (see above)
  - `<optional>`: Still compiles (with non-fatal warning about `_Hash_bytes` template)
  - `<any>`: Still compiles (with non-fatal warning about `_Hash_bytes` template)

### 2026-01-19 (Template Profiling & Progress Logging Improvements)
- **Enhanced template instantiation progress logging:** Added periodic progress reports during template instantiation
  - Logs every 100 template instantiations with elapsed time, cache hit rate, and instantiation depth
  - Example: `[Progress] 400 template instantiations in 8 ms (cache hit rate: 59.7%)`
  - Helps identify where compilation gets stuck during template-heavy headers
- **Template instantiation tracking:** Added start/end tracking for individual template instantiations
  - Tracks current instantiation depth to identify recursive template issues
  - Useful for debugging infinite loops or extremely slow recursive instantiation
- **Build with info logging:** Use `-DFLASHCPP_LOG_LEVEL=2` to enable progress logging in release builds
  - Create dedicated InfoRelease build: `clang++ -DFLASHCPP_LOG_LEVEL=2 -O3 ...`
- **Findings from extended timeout testing:**
  - `<concepts>` completes 400 instantiations in 8ms then gets stuck (likely infinite recursion or loop)
  - `<type_traits>` compiles successfully with ~400 template instantiations, 59.7% cache hit rate
  - `<string_view>` and `<string>` timeout at 60+ seconds (true performance issue, not parse error)
  - Many headers marked as "timeout" actually fail silently with exit code 1

### 2026-01-19 (Progress Logging & Analysis)
- **Progress logging added:** Parser now logs progress every 500 top-level nodes with elapsed time
  - Build with `-DFLASHCPP_LOG_LEVEL=2` to see Info-level progress messages
  - Example: `[Progress] Parsed 500 top-level nodes in 150 ms`
  - Final summary: `[Progress] Parsing complete: 1200 top-level nodes, 5000 AST nodes in 2500 ms`
- **`<vector>` root cause identified:** Fails during `<type_traits>` processing, not `<vector>` itself
  - Error occurs at `bits/utility.h:56` pattern: `typename _Up = typename remove_cv<_Tp>::type`
  - The parse succeeds for standalone test cases but fails when embedded in full `<type_traits>` context
  - Issue is likely related to template instantiation state during complex nested template contexts
- **Performance insight:** `<type_traits>` alone takes ~10ms for 8 top-level nodes due to heavy template instantiation

### 2026-01-19 (Parse Error Fixes)
- **`__typeof__` GCC extension:** Works like `decltype` for GCC compatibility
- **Using-declarations:** `using Base::member;` now imports member names into derived class scope
- **Enum forward declarations:** `enum class byte : unsigned char;` now parses correctly
- **`<ratio>` now compiles:** ~1.4 seconds (was marked as timeout)

### 2026-01-19 (Comprehensive Audit)
- Re-tested all headers with extended timeouts; many "timeouts" are actually parse errors
- Identified specific blockers: variadic non-type params, complex decltype patterns
- True timeouts: `<concepts>`, `<bit>`, `<string_view>`, `<string>`, `<ranges>`

### 2026-01-18
- Log level bug fix, pointer-to-member typedef, trailing return type params
- New headers: `<stdfloat>`, `<spanstream>`, `<print>`, `<expected>`, `<barrier>`, `<stacktrace>`

### 2026-01-17
- Friend functions, out-of-line ctors/dtors, elaborated type specifiers, `__builtin_strlen`
- **Test case:** `test_parens_less_than_ret0.cpp`
- **Impact:** `<cwctype>` now compiles (~0.78s)

### 2026-01-15
- **Pointer-to-void conversion:** Any pointer type can now convert to `void*` in overload resolution
- **Nested anonymous struct/union:** Deep nesting in typedef declarations now works
- **Function pointer members:** Anonymous structs can now contain function pointer members
- **Target-dependent `long` size:** Windows (LLP64) = 32 bits, Linux (LP64) = 64 bits
- **Object-like macros:** Parenthesized bodies with `sizeof` no longer misparse as function-like
- **Static constexpr visibility:** Members visible in partial specialization bodies
- **Ternary in template args:** `integral_constant<intmax_t, (x < 0) ? -1 : 1>` works
- **Function pointer params:** Pack expansion, noexcept, pointer params in function pointer types
- **Typedef array syntax:** `typedef long int __jmp_buf[8];` supported
- **Test cases:** `test_ptr_to_void_conversion_ret0.cpp`, `test_deep_nested_anon_struct_ret0.cpp`, `test_object_macro_with_parens_ret32.cpp`, `test_partial_spec_static_member_visibility_ret0.cpp`, `test_ternary_in_template_arg_ret0.cpp`, `test_funcptr_param_pack_ret0.cpp`, `test_typedef_array_ret0.cpp`
- **Impact:** `<new>` compiles (~0.5s), `<csetjmp>` compiles (~0.2s), `<csignal>` parses

### 2026-01-14
- **Brace init for template structs:** `ns::Pair<int> p = {1, 2};` works for `Type::UserDefined`
- **Variable templates in type context:** `enable_if<is_reference_v<T>, U>` no longer fails
- **Template argument reference preservation:** `test<int&>()` correctly preserves reference qualifier
- **Template alias with requires clause:** `template<typename T> requires C<T> using Alias = T;`
- **Template member constructors:** `template<typename U> Box(const Box<U>& other)` parsed correctly
- **Namespace-qualified variable template lookup:** Fixed resolution in same-namespace function templates
- **Non-type template params in return types:** `typename tuple_element<_Int, pair<...>>::type&` works
- **Test cases:** `test_template_brace_init_userdefined_ret3.cpp`, `test_variable_template_in_enable_if_ret0.cpp`, `test_template_ref_preservation_ret0.cpp`, `test_requires_clause_alias_ret0.cpp`, `test_template_ctor_ret0.cpp`, `test_ns_var_template_ret0.cpp`, `test_nontype_template_param_return_ret0.cpp`
- **Impact:** `<type_traits>` compiles in ~8s, `<utility>` progresses further

### 2026-01-13
- **Member template function calls:** `Helper<T>::Check<U>()` works
- **Template friend declarations:** `template<typename T1, T2> friend struct pair;` supported
- **Variable template brace init:** `inline constexpr Type<T> name{};` works
- **C++17 nested namespaces:** `namespace A::B::C { }` supported
- **C++20 inline nested namespaces:** `namespace A::inline B { }` supported
- **`const typename` in type specifiers:** `constexpr const typename T::type` works
- **Library type traits vs intrinsics:** `__is_swappable<T>` treated as template, `__is_void(T)` as intrinsic
- **Test cases:** `test_member_template_call_ret0.cpp`, `test_template_friend_decl_ret0.cpp`, `test_var_template_brace_init_ret0.cpp`, `test_nested_namespace_ret42.cpp`

## See Also

- [`STANDARD_HEADERS_MISSING_FEATURES.md`](./STANDARD_HEADERS_MISSING_FEATURES.md) - Detailed analysis of missing features and implementation history
