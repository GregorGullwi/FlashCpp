# Standard Header Tests

This directory contains test files for C++ standard library headers to assess FlashCpp's compatibility with the C++ standard library.

## Current Status

| Header | Test File | Status | Notes |
|--------|-----------|--------|-------|
| `<limits>` | `test_std_limits.cpp` | ✅ Compiled | ~197ms |
| `<type_traits>` | `test_std_type_traits.cpp` | ✅ Compiled | ~230ms; unary trait constants synthesized (2026-02-04, emits zero-init logs for integral_constant::value) |
| `<compare>` | `test_std_compare_ret42.cpp` | ✅ Compiled | ~655ms (2026-02-23: Updated timing; sibling namespace fix confirmed) |
| `<version>` | N/A | ✅ Compiled | ~34ms |
| `<source_location>` | N/A | ✅ Compiled | ~34ms |
| `<numbers>` | N/A | ✅ Compiled | ~249ms |
| `<initializer_list>` | N/A | ✅ Compiled | ~26ms |
| `<ratio>` | `test_std_ratio.cpp` | ✅ Compiled | ~367ms (2026-02-11: Fixed with self-referential template handling and dependent type detection) |
| `<optional>` | `test_std_optional.cpp` | ✅ Compiled | ~2685ms (2026-02-23: Re-enabled after dependent name and ptr_traits.h fixes) |
| `<any>` | `test_std_any.cpp` | ✅ Compiled | ~586ms (2026-02-23: Fixed enum enumerator scope resolution in member functions) |
| `<utility>` | `test_std_utility.cpp` | ✅ Compiled | ~415ms (2026-02-23: Updated timing; sibling namespace fix confirmed) |
| `<concepts>` | `test_std_concepts.cpp` | ✅ Compiled | ~265ms |
| `<bit>` | N/A | ✅ Compiled | ~328ms (2026-02-06: Fixed with `__attribute__` and type trait whitelist fixes) |
| `<string_view>` | `test_std_string_view.cpp` | ✅ Compiled | ~5554ms (2026-02-23: Fixed with template recursion depth increase 10→64) |
| `<string>` | `test_std_string.cpp` | ❌ Parse Error | Hits MAX_RECURSION_DEPTH (50) in parse_expression |
| `<array>` | `test_std_array.cpp` | ❌ Parse Error | static_assert fails during template instantiation (AST node is not an expression) |
| `<algorithm>` | `test_std_algorithm.cpp` | ❌ Parse Error | static_assert fails during template instantiation (AST node is not an expression) |
| `<span>` | `test_std_span.cpp` | ✅ Compiled | ~1972ms (2026-02-23: Fixed with template recursion depth increase and non-type param substitution) |
| `<tuple>` | `test_std_tuple.cpp` | ❌ Parse Error | static_assert fails during template instantiation (AST node is not an expression) |
| `<vector>` | `test_std_vector.cpp` | ❌ Parse Error | static_assert fails during template instantiation (AST node is not an expression) |
| `<memory>` | `test_std_memory.cpp` | ❌ Parse Error | static_assert fails during template instantiation (AST node is not an expression) |
| `<functional>` | `test_std_functional.cpp` | ❌ Parse Error | static_assert fails during template instantiation (AST node is not an expression) |
| `<map>` | `test_std_map.cpp` | ❌ Parse Error | static_assert fails during template instantiation (AST node is not an expression) |
| `<set>` | `test_std_set.cpp` | ❌ Parse Error | static_assert fails during template instantiation (AST node is not an expression) |
| `<ranges>` | `test_std_ranges.cpp` | ❌ Parse Error | Hits MAX_RECURSION_DEPTH (50) in parse_expression |
| `<iostream>` | `test_std_iostream.cpp` | ❌ Parse Error | Hits MAX_RECURSION_DEPTH (50) in parse_expression |
| `<chrono>` | `test_std_chrono.cpp` | ❌ Parse Error | Variable template evaluation in constant expressions not supported (__is_ratio_v) |
| `<atomic>` | N/A | ✅ Compiled | ~6105ms (2026-02-23: Fixed with enum enumerator scope resolution; some static_assert warnings remain) |
| `<new>` | N/A | ✅ Compiled | ~44ms |
| `<exception>` | N/A | ✅ Compiled | ~471ms |
| `<typeinfo>` | N/A | ✅ Compiled | ~41ms (2026-02-05: Fixed with _Complex and __asm support) |
| `<typeindex>` | N/A | ✅ Compiled | ~766ms (2026-02-05: Fixed with _Complex and __asm support) |
| `<numeric>` | N/A | ✅ Compiled | ~884ms (2026-02-13: Compiles successfully) |
| `<variant>` | `test_std_variant.cpp` | ❌ Parse Error | static_assert fails during template instantiation (AST node is not an expression) |
| `<csetjmp>` | N/A | ✅ Compiled | ~27ms |
| `<csignal>` | N/A | ✅ Compiled | ~101ms (2026-02-13: Now compiles successfully) |
| `<stdfloat>` | N/A | ✅ Compiled | ~14ms (C++23) |
| `<spanstream>` | N/A | ✅ Compiled | ~34ms (C++23) |
| `<print>` | N/A | ✅ Compiled | ~34ms (C++23) |
| `<expected>` | N/A | ✅ Compiled | ~33ms (C++23) |
| `<text_encoding>` | N/A | ✅ Compiled | ~34ms (C++26) |
| `<stacktrace>` | N/A | ✅ Compiled | ~35ms (C++23) |
| `<barrier>` | N/A | ❌ Parse Error | static_assert fails during template instantiation (AST node is not an expression) |
| `<coroutine>` | N/A | ❌ Parse Error | ~31ms; fails on coroutine-specific syntax (requires -fcoroutines) |
| `<latch>` | `test_std_latch.cpp` | ❌ Codegen Error | ~4438ms (2026-02-23: `memory_order_relaxed`/`__memory_order_mask` lookup fixed; now fails on `_Size` symbol) |
| `<shared_mutex>` | N/A | ❌ Parse Error | Variable template evaluation in constant expressions not supported (__is_ratio_v) |
| `<cstdlib>` | N/A | ✅ Compiled | ~84ms |
| `<cstdio>` | N/A | ✅ Compiled | ~53ms |
| `<cstring>` | N/A | ✅ Compiled | ~49ms |
| `<cctype>` | N/A | ✅ Compiled | ~45ms |
| `<cwchar>` | N/A | ✅ Compiled | ~51ms |
| `<cwctype>` | N/A | ✅ Compiled | ~58ms |
| `<cerrno>` | N/A | ✅ Compiled | ~26ms |
| `<cassert>` | N/A | ✅ Compiled | ~25ms |
| `<cstdarg>` | N/A | ✅ Compiled | ~24ms |
| `<cstddef>` | N/A | ✅ Compiled | ~41ms |
| `<cstdint>` | N/A | ✅ Compiled | ~28ms |
| `<cinttypes>` | N/A | ✅ Compiled | ~32ms |
| `<cuchar>` | N/A | ✅ Compiled | ~58ms |
| `<cfenv>` | N/A | ✅ Compiled | ~30ms |
| `<clocale>` | N/A | ✅ Compiled | ~29ms |
| `<ctime>` | N/A | ✅ Compiled | ~44ms |
| `<climits>` | N/A | ✅ Compiled | ~24ms |
| `<cfloat>` | N/A | ✅ Compiled | ~25ms |
| `<cmath>` | `test_std_cmath.cpp` | ❌ Timeout | >30s; hangs during codegen |

**Legend:** ✅ Compiled | ❌ Failed/Parse/Include Error | ⏱️ Timeout (60s) | 💥 Crash

### Summary (2026-02-23)

**Total headers tested:** 68
**Compiling successfully:** 46 (68%)
**Parse errors:** 14
**Codegen errors (parsing completes):** 1 (`<latch>`)
**Timeout:** 1 (`<cmath>`)

### Known Blockers

The most impactful blockers preventing more headers from compiling:

1. **`static_assert` evaluation with complex template expressions**: Many headers fail because `static_assert` conditions involving variable templates, nested type traits, or complex template expressions cannot be evaluated. The AST node is not recognized as an expression during template instantiation. Affects: `<array>`, `<tuple>`, `<vector>`, `<memory>`, `<functional>`, `<map>`, `<set>`, `<variant>`, `<barrier>`, `<algorithm>`.

2. **Variable template evaluation in constant expressions**: Variable templates like `__is_ratio_v<T>` or `is_nothrow_convertible_v<T,U>` are not evaluated to their constexpr values during template argument resolution. They're treated as types instead of values, causing incorrect `integral_constant::value` initialization. Affects: `<chrono>`, `<shared_mutex>`.

3. **`parse_expression` recursion depth limit (50)**: Some complex template expressions exceed the expression parser's recursion depth of 50. Affects: `<string>`, `<ranges>`, `<iostream>`.

4. **Non-type template parameter substitution in codegen**: Some non-type template parameters (like `_Size` in `<latch>`) are still not resolved during code generation when they go through complex template instantiation chains involving variable templates.

### Recent Fixes (2026-02-23)

1. **Template recursion depth limit increased (10→64)**: The `try_instantiate_template` recursion depth limit was raised from 10 to 64, matching common compiler defaults. This unblocked `<string_view>` and `<span>` which needed deeper template instantiation chains (e.g., `__niter_base`).

2. **Non-type template parameters in deferred base class substitution**: The `name_substitution_map` in `try_instantiate_class_template` now includes non-type template parameters (previously filtered to Type-kind only). This fixes `integral_constant<T,v>::value` codegen when `integral_constant` is instantiated through inheritance chains like `extent_helper<N> : integral_constant<unsigned long long, N>`.

3. **Enum enumerator scope resolution in member functions**: Added lookup of unscoped enum enumerators within the enclosing class scope during code generation. Previously, enumerators like `_Op_clone` from a nested `enum _Op` inside `std::any` were not found in symbol tables during member function codegen. This unblocked `<any>`, `<atomic>`, and partially `<latch>`.

4. **Namespace-scope identifier fallback in codegen**: Added global-qualified fallback lookup for unresolved identifiers during code generation (including parent-namespace lookup and unique namespace-qualified fallback). This resolved `<latch>` failures on `memory_order_relaxed` and `__memory_order_mask` and exposed the remaining `_Size` non-type template substitution gap.

5. **GCC atomic predefined macros**: Added missing predefined macros (`__GCC_ATOMIC_*` and `__GCC_ATOMIC_TEST_AND_SET_TRUEVAL`) in GCC/Clang compatibility mode to match libstdc++ expectations and avoid unresolved macro identifiers during `<atomic>/<latch>` compilation paths.
