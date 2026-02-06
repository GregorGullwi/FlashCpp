# Standard Header Tests

This directory contains test files for C++ standard library headers to assess FlashCpp's compatibility with the C++ standard library.

## Current Status

| Header | Test File | Status | Notes |
|--------|-----------|--------|-------|
| `<limits>` | `test_std_limits.cpp` | ✅ Compiled | ~29ms |
| `<type_traits>` | `test_std_type_traits.cpp` | ✅ Compiled | ~187ms; unary trait constants synthesized (2026-02-04, emits zero-init logs for integral_constant::value) |
| `<compare>` | N/A | ✅ Compiled | ~258ms (2026-01-24: Fixed with operator[], brace-init, and throw expression fixes) |
| `<version>` | N/A | ✅ Compiled | ~17ms |
| `<source_location>` | N/A | ✅ Compiled | ~17ms |
| `<numbers>` | N/A | ✅ Compiled | ~33ms |
| `<initializer_list>` | N/A | ✅ Compiled | ~16ms |
| `<ratio>` | `test_std_ratio.cpp` | 💥 Crash | glibc malloc assertion failure (memory corruption) |
| `<vector>` | `test_std_vector.cpp` | ❌ Parse Error | Blocked by missing features in allocator/string includes |
| `<tuple>` | `test_std_tuple.cpp` | ❌ Parse Error | `static_assert` failed in `uses_allocator.h:106` |
| `<optional>` | `test_std_optional.cpp` | ❌ Parse Error | Unnamed `in_place_t` parameter at `optional:564` |
| `<variant>` | `test_std_variant.cpp` | ❌ Parse Error | `alignas` in type specifier at `aligned_buffer.h:56` |
| `<any>` | `test_std_any.cpp` | ❌ Parse Error | Expected type specifier at `any:583` (out-of-line template member) |
| `<concepts>` | `test_std_concepts.cpp` | ✅ Compiled | ~100ms |
| `<utility>` | `test_std_utility.cpp` | ✅ Compiled | ~311ms (2026-01-30: Fixed with dependent template instantiation fix) |
| `<bit>` | N/A | ✅ Compiled | ~80ms (2026-02-06: Fixed with `__attribute__` and type trait whitelist fixes) |
| `<string_view>` | `test_std_string_view.cpp` | ❌ Parse Error | Blocked by allocator includes |
| `<string>` | `test_std_string.cpp` | ❌ Parse Error | `::operator new()` in `static_cast` at `new_allocator.h:148` |
| `<array>` | `test_std_array.cpp` | ❌ Parse Error | Blocked by allocator includes |
| `<memory>` | `test_std_memory.cpp` | ❌ Parse Error | Blocked by allocator includes |
| `<functional>` | `test_std_functional.cpp` | ❌ Parse Error | `static_assert` failed in `uses_allocator.h:106` |
| `<algorithm>` | `test_std_algorithm.cpp` | ❌ Parse Error | Blocked by allocator includes |
| `<map>` | `test_std_map.cpp` | ❌ Parse Error | Blocked by allocator includes |
| `<set>` | `test_std_set.cpp` | ❌ Parse Error | Blocked by allocator includes |
| `<span>` | `test_std_span.cpp` | ❌ Parse Error | Blocked by allocator includes |
| `<ranges>` | `test_std_ranges.cpp` | ❌ Parse Error | Blocked by allocator includes |
| `<iostream>` | `test_std_iostream.cpp` | ❌ Parse Error | `::operator new()` in `static_cast` at `new_allocator.h:148` (2026-02-06: progressed past `__attribute__`, `__ATOMIC_ACQ_REL`, `__is_single_threaded`) |
| `<chrono>` | `test_std_chrono.cpp` | 💥 Crash | glibc malloc assertion failure (memory corruption) |
| `<atomic>` | N/A | ❌ Parse Error | `__cmpexch_failure_order2` overload resolution at `atomic_base.h:128` (2026-02-06: progressed past `static_assert sizeof`) |
| `<new>` | N/A | ✅ Compiled | ~18ms |
| `<exception>` | N/A | ✅ Compiled | ~43ms |
| `<typeinfo>` | N/A | ✅ Compiled | ~43ms (2026-02-05: Fixed with _Complex and __asm support) |
| `<typeindex>` | N/A | ✅ Compiled | ~43ms (2026-02-05: Fixed with _Complex and __asm support) |
| `<csetjmp>` | N/A | ✅ Compiled | ~16ms |
| `<csignal>` | N/A | ❌ Parse Error | `__attribute_deprecated_msg__` at `signal.h:368` (pre-existing, depends on system headers) |
| `<stdfloat>` | N/A | ✅ Compiled | ~14ms (C++23) |
| `<spanstream>` | N/A | ✅ Compiled | ~17ms (C++23) |
| `<print>` | N/A | ✅ Compiled | ~17ms (C++23) |
| `<expected>` | N/A | ✅ Compiled | ~18ms (C++23) |
| `<text_encoding>` | N/A | ✅ Compiled | ~17ms (C++26) |
| `<barrier>` | N/A | ❌ Parse Error | `__cmpexch_failure_order2` overload resolution at `atomic_base.h:128` (2026-02-06: progressed past `static_assert sizeof`) |
| `<stacktrace>` | N/A | ✅ Compiled | ~17ms (C++23) |
| `<coroutine>` | N/A | ❌ Parse Error | `Expected identifier token` at `coroutine:297` (2026-02-06: progressed past `operator bool()`) |

**Legend:** ✅ Compiled | ❌ Failed/Parse/Include Error | ⏱️ Timeout (60s) | 💥 Crash

### Recent Fixes (2026-02-06)

The following parser issues were fixed to unblock standard header compilation:

1. **GCC `__attribute__((...))` between return type and function name**: `parse_type_and_name()` now skips `__attribute__` specifications that appear after the return type but before the function name (e.g., `_Atomic_word __attribute__((__always_inline__)) __exchange_and_add(...)`). This unblocks `atomicity.h` used by `<iostream>`.

2. **`__ATOMIC_*` memory ordering macros**: Added `__ATOMIC_RELAXED` (0), `__ATOMIC_CONSUME` (1), `__ATOMIC_ACQUIRE` (2), `__ATOMIC_RELEASE` (3), `__ATOMIC_ACQ_REL` (4), `__ATOMIC_SEQ_CST` (5) as predefined macros. These are used by `<atomic>` and `<iostream>` via `atomicity.h`.

3. **Type trait whitelist instead of prefix matching**: The expression parser now uses a whitelist of known type traits (`__is_void`, `__is_integral`, etc.) instead of matching all identifiers with `__is_*` or `__has_*` prefixes. This prevents regular functions like `__gnu_cxx::__is_single_threaded()` from being misidentified as type trait intrinsics. Unblocks `<iostream>`.

4. **Conversion operator detection in template struct bodies**: Template specialization struct body parsing now correctly detects conversion operators like `constexpr explicit operator bool() const noexcept`. Previously these failed with "Unexpected token in type specifier: 'operator'" because `parse_type_and_name()` doesn't handle conversion operators. This progresses `<coroutine>`.

5. **`sizeof` returning 0 for dependent types**: `evaluate_sizeof()` now returns a `TemplateDependentExpression` error instead of 0 when the type size is unknown. In standard C++, `sizeof` never returns 0, so a zero result indicates an incomplete or dependent type. This prevents false `static_assert` failures in templates.

6. **Improved `static_assert` deferral in template contexts**: `parse_static_assert()` now always defers evaluation when the condition contains template-dependent expressions (regardless of parsing context), and also defers in template struct bodies when evaluation fails for any reason. This unblocks `<atomic>` and `<barrier>` past the `static_assert(sizeof(__waiter_type) == sizeof(__waiter_pool_base))` check.

### Current Blockers for Major Headers

| Blocker | Affected Headers | Details |
|---------|-----------------|---------|
| `::operator new()` in expressions | `<string>`, `<iostream>`, many others | `static_cast<_Tp*>(::operator new(__n * sizeof(_Tp)))` — global scope `operator new` call in expressions |
| `__cmpexch_failure_order2` overload | `<atomic>`, `<barrier>` | Constexpr function using bitwise ops on `memory_order` enum |
| `alignas` in type specifier | `<variant>` | `alignas(...)` used inside `aligned_buffer.h` struct |
| Unnamed `in_place_t` parameter | `<optional>` | `_Optional_base(in_place_t, _Args&&...)` unnamed parameter |
| Memory corruption | `<ratio>`, `<chrono>` | glibc malloc assertion failure during parsing |

### Recent Fixes (2026-02-05)

The following parser issues were fixed to unblock standard header compilation:

1. **`__typeof(function_name)`**: `get_expression_type()` now handles `FunctionDeclarationNode` identifiers, returning the function's return type. This unblocks `c++locale.h` which uses `extern "C" __typeof(uselocale) __uselocale;`.

2. **`__builtin_va_list` / `__gnuc_va_list`**: Registered as built-in types in `initialize_native_types()` and handled in `parse_type_specifier()`. This unblocks `c++locale.h` and any code using variadic argument types directly.

3. **Unnamed bitfields (`int :32;`)**: `parse_type_and_name()` now recognizes `:` and `;` as valid terminators for unnamed declarations, and struct member parsing consumes the bitfield width expression. This unblocks `bits/timex.h` (included via `<time.h>` → `<pthread.h>`).

4. **Function pointer params with pointer return types**: Added a second function pointer detection check in `parse_type_and_name()` after pointer levels are consumed. The pattern `void *(*callback)(void *)` was previously not detected because the `(` check happened before `*` was consumed. This unblocks `<pthread.h>` function declarations.

5. **C-style `(void)` parameter lists**: `parse_parameter_list()` now treats `(void)` as equivalent to `()` (no parameters). This unblocks many C library function declarations like `pthread_self(void)`.

**Remaining blocker for `<iostream>`, `<atomic>`, `<barrier>`**: The `pthread_self()` function call in `gthr-default.h:700` fails with "No matching function" — the function is declared and found in the symbol table, but call resolution doesn't match the `(void)` signature. This is now fixed by the `(void)` parameter list change above.

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

