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
| `<ratio>` | `test_std_ratio.cpp` | ✅ Compiled | ~183ms (2026-02-11: Fixed with self-referential template handling and dependent type detection) |
| `<vector>` | `test_std_vector.cpp` | ❌ Timeout | Preprocessing completes (~248ms, 42567 lines); hangs during template instantiation |
| `<tuple>` | `test_std_tuple.cpp` | ❌ Timeout | Hangs during template instantiation |
| `<optional>` | `test_std_optional.cpp` | ✅ Compiled | ~759ms (2026-02-08: Fixed with ref-qualifier, explicit constexpr, and attribute fixes) |
| `<variant>` | `test_std_variant.cpp` | ❌ Parse Error | Progressed from line 499→1137; 5 separate fixes: func template call disambiguation, member variable template partial spec, nested templates in member struct bodies, func pointer pack expansion in type aliases. Now fails at `variant:1137` (struct body boundary tracking issue) |
| `<any>` | `test_std_any.cpp` | ✅ Compiled | ~300ms (previously blocked by out-of-line template member) |
| `<concepts>` | `test_std_concepts.cpp` | ✅ Compiled | ~100ms |
| `<utility>` | `test_std_utility.cpp` | ✅ Compiled | ~311ms (2026-01-30: Fixed with dependent template instantiation fix) |
| `<bit>` | N/A | ✅ Compiled | ~80ms (2026-02-06: Fixed with `__attribute__` and type trait whitelist fixes) |
| `<string_view>` | `test_std_string_view.cpp` | ✅ Compiled | ~786ms (2026-02-11: Parsing completes; object file generated with codegen warnings) |
| `<string>` | `test_std_string.cpp` | ❌ Codegen Error | Parsing completes; fails during IR conversion (`bad_any_cast` in template member body) |
| `<array>` | `test_std_array.cpp` | ✅ Compiled | ~738ms (2026-02-08: Fixed with deduction guide and namespace-qualified call fixes) |
| `<memory>` | `test_std_memory.cpp` | ❌ Timeout | Hangs during template instantiation (depends on `<tuple>`) |
| `<functional>` | `test_std_functional.cpp` | ❌ Timeout | Hangs during template instantiation (depends on `<tuple>`) |
| `<algorithm>` | `test_std_algorithm.cpp` | ✅ Compiled | ~866ms (2026-02-11: Parsing completes; object file generated with codegen warnings) |
| `<map>` | `test_std_map.cpp` | ❌ Codegen Error | Progressed from parse errors to `bad_any_cast` during IR conversion (function template call fix helped) |
| `<set>` | `test_std_set.cpp` | ❌ Codegen Error | Progressed from parse errors to `bad_any_cast` during IR conversion (function template call fix helped) |
| `<span>` | `test_std_span.cpp` | ✅ Compiled | ~500ms (2026-02-11: Parsing completes; object file generated) |
| `<ranges>` | `test_std_ranges.cpp` | ❌ Codegen Error | Parsing completes; fails during IR conversion (`bad_any_cast` in template member body) |
| `<iostream>` | `test_std_iostream.cpp` | ❌ Codegen Error | Parsing completes; fails during IR conversion (`bad_any_cast` in template member body) |
| `<chrono>` | `test_std_chrono.cpp` | ❌ Timeout | Preprocessing and parsing complete; hangs during template instantiation (no longer fails at forward declaration) |
| `<atomic>` | N/A | ❌ Codegen Error | ~492ms (2026-02-13: Parsing completes after member function call shadowing fix; codegen fails on `_Size` symbol) |
| `<new>` | N/A | ✅ Compiled | ~18ms |
| `<exception>` | N/A | ✅ Compiled | ~43ms |
| `<typeinfo>` | N/A | ✅ Compiled | ~43ms (2026-02-05: Fixed with _Complex and __asm support) |
| `<typeindex>` | N/A | ✅ Compiled | ~43ms (2026-02-05: Fixed with _Complex and __asm support) |
| `<csetjmp>` | N/A | ✅ Compiled | ~16ms |
| `<csignal>` | N/A | ✅ Compiled | ~133ms (2026-02-13: Now compiles successfully) |
| `<stdfloat>` | N/A | ✅ Compiled | ~14ms (C++23) |
| `<spanstream>` | N/A | ✅ Compiled | ~17ms (C++23) |
| `<print>` | N/A | ✅ Compiled | ~17ms (C++23) |
| `<expected>` | N/A | ✅ Compiled | ~18ms (C++23) |
| `<text_encoding>` | N/A | ✅ Compiled | ~17ms (C++26) |
| `<barrier>` | N/A | ❌ Timeout | Hangs during template instantiation (depends on `<atomic>`) |
| `<stacktrace>` | N/A | ✅ Compiled | ~17ms (C++23) |
| `<coroutine>` | N/A | ❌ Timeout | Hangs during template instantiation (infinite loop) |
| `<numeric>` | N/A | ✅ Compiled | ~911ms (2026-02-13: Compiles successfully) |
| `<latch>` | N/A | ❌ Codegen Error | Parsing completes ~382ms; fails on `_Size` symbol (non-type template param not substituted) |
| `<shared_mutex>` | N/A | ❌ Codegen Error | Parsing completes ~574ms; fails on `_S_epoch_diff` symbol |
| `<cstdlib>` | N/A | ✅ Compiled | (2026-02-13) |
| `<cstdio>` | N/A | ✅ Compiled | (2026-02-13) |
| `<cstring>` | N/A | ✅ Compiled | (2026-02-13) |
| `<cctype>` | N/A | ✅ Compiled | (2026-02-13) |
| `<cwchar>` | N/A | ✅ Compiled | (2026-02-13) |
| `<cwctype>` | N/A | ✅ Compiled | (2026-02-13) |
| `<cerrno>` | N/A | ✅ Compiled | (2026-02-13) |
| `<cassert>` | N/A | ✅ Compiled | (2026-02-13) |
| `<cstdarg>` | N/A | ✅ Compiled | (2026-02-13) |
| `<cstddef>` | N/A | ✅ Compiled | (2026-02-13) |
| `<cstdint>` | N/A | ✅ Compiled | (2026-02-13) |
| `<cinttypes>` | N/A | ✅ Compiled | (2026-02-13) |
| `<cuchar>` | N/A | ✅ Compiled | (2026-02-13) |
| `<cfenv>` | N/A | ✅ Compiled | (2026-02-13) |
| `<clocale>` | N/A | ✅ Compiled | (2026-02-13) |
| `<ctime>` | N/A | ✅ Compiled | (2026-02-13) |
| `<climits>` | N/A | ✅ Compiled | (2026-02-13) |
| `<cfloat>` | N/A | ✅ Compiled | (2026-02-13) |
| `<cmath>` | `test_std_cmath.cpp` | ❌ Codegen Error | Parsing completes after preprocessor fix (empty trailing macro args + ## token-pasting order + arg pre-expansion); codegen fails on `int` symbol in `__ellint_rf` |

**Legend:** ✅ Compiled | ❌ Failed/Parse/Include Error | ⏱️ Timeout (60s) | 💥 Crash

### Recent Fixes (2026-02-13, preprocessor)

1. **Empty trailing macro arguments not captured**: `splitArgs("a, ")` returned `["a"]` instead of `["a", ""]`. Fixed to correctly push an empty trailing argument when the arg string ends after a comma. This is critical for glibc's `__MATHDECL_ALIAS` pattern which passes empty suffix arguments.

2. **## token-pasting processed after rescanning instead of before**: Per C standard 6.10.3.3, `##` must be processed after argument substitution but before rescanning. FlashCpp was doing it after rescanning, causing `__CONCAT` expansions to corrupt identifiers in nested macro chains.

3. **Macro arguments not pre-expanded before substitution**: Per C standard 6.10.3.1, arguments not adjacent to `#` or `##` must be fully expanded before being substituted into the replacement list. Without this, nested macro chains like `__SIMD_DECL(__MATH_PRECNAME(func, suffix))` would paste unexpanded macro names instead of their results.

### Recent Fixes (2026-02-13)

1. **Member function call shadowed by template function lookup**: When parsing a member function body inside a template struct, a call like `compare_exchange_weak(expected, desired, order, order)` would find the name in the template registry even though it was already resolved as a class member function. Fix: skip template registry lookup when `found_member_function_in_context` is already true. Unblocks `<atomic>` parsing.

2. **`using Base::operator Type;` not parsed**: Using-declarations for conversion operators and assignment operators (e.g., `using __base_type::operator __integral_type;`) hit the `else { break; }` branch because `operator` is a keyword. Fix: handle `operator` keyword after `::` and build the full operator name.

3. **Bool/Int type mismatch in partial specialization pattern matching**: Default bool template arguments (`template<typename T, bool = false>`) were stored as `Type::Int` during fill-in but `Type::Bool` in patterns, causing pattern matching to fail silently. Fix: use `Type::Bool` for bool defaults, and allow `Bool`/`Int` interchangeability in pattern matching for non-type value parameters. This fix enables partial specialization member function codegen.

4. **Overload resolution ambiguity**: When multiple overloads tie on conversion rank (e.g., `f(T*)` vs `f(volatile T*)`), the resolver now picks the first match instead of returning ambiguous.

5. **Codegen assert hang**: `assert(false)` in `generateIdentifierIr` replaced with `throw std::runtime_error()` per existing convention, preventing SIGABRT hangs.

