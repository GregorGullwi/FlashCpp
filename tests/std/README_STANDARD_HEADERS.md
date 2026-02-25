# Standard Header Tests

This directory contains test files for C++ standard library headers to assess FlashCpp's compatibility with the C++ standard library.

## Current Status

| Header | Test File | Status | Notes |
|--------|-----------|--------|-------|
| `<limits>` | `test_std_limits.cpp` | ✅ Compiled | ~184ms (2026-02-24: Fixed register allocator crash on X64Register::Count) |
| `<type_traits>` | `test_std_type_traits.cpp` | ✅ Compiled | ~228ms (2026-02-24: Fixed namespace-qualified struct lookup in constexpr evaluator) |
| `<compare>` | `test_std_compare_ret42.cpp` | ✅ Compiled | ~655ms (2026-02-23: Updated timing; sibling namespace fix confirmed) |
| `<version>` | N/A | ✅ Compiled | ~34ms |
| `<source_location>` | N/A | ✅ Compiled | ~34ms |
| `<numbers>` | N/A | ✅ Compiled | ~249ms |
| `<initializer_list>` | N/A | ✅ Compiled | ~26ms |
| `<ratio>` | `test_std_ratio.cpp` | ❌ Parse Error | Variable template evaluation (__is_ratio_v) not supported in static_assert context |
| `<optional>` | `test_std_optional.cpp` | ✅ Compiled | ~911ms (2026-02-24: Updated timing) |
| `<any>` | `test_std_any.cpp` | ✅ Compiled | ~374ms (2026-02-24: Updated timing) |
| `<utility>` | `test_std_utility.cpp` | ❌ Codegen Error | ~349ms parse; member 'first' not found in pair instantiation during codegen |
| `<concepts>` | `test_std_concepts.cpp` | ✅ Compiled | ~257ms (2026-02-24: Updated timing) |
| `<bit>` | N/A | ✅ Compiled | ~328ms (2026-02-06: Fixed with `__attribute__` and type trait whitelist fixes) |
| `<string_view>` | `test_std_string_view.cpp` | ❌ Codegen Error | ~1187ms parse; fold expression/pack expansion not expanded during template instantiation |
| `<string>` | `test_std_string.cpp` | ❌ Codegen Crash | Parses successfully (2026-02-24: parent namespace lookup fix); crashes during codegen (PackExpansionExprNode) |
| `<array>` | `test_std_array.cpp` | ❌ Parse Error | Aggregate brace initialization not supported for template types (`std::array<int,5> arr = {1,2,3,4,5}`) |
| `<algorithm>` | `test_std_algorithm.cpp` | ❌ Codegen Error | Parses OK; `__i` auto parameter not resolved in subrange constructor codegen |
| `<span>` | `test_std_span.cpp` | ❌ Codegen Error | ~926ms parse; fold expression/pack expansion not expanded during template instantiation |
| `<tuple>` | `test_std_tuple.cpp` | ❌ Codegen Error | `__i` auto parameter not resolved in subrange constructor codegen |
| `<vector>` | `test_std_vector.cpp` | ❌ Codegen Error | Parses now (2026-02-24: alias template fix); member `_M_start` not found in `_Vector_impl` during codegen |
| `<memory>` | `test_std_memory.cpp` | ❌ Parse Error | Fails on ADL-based `make_error_code` call in `<system_error>` |
| `<functional>` | `test_std_functional.cpp` | ❌ Parse Error | Base class `__hash_code_base` not found in `<hashtable.h>` (dependent base class) |
| `<map>` | `test_std_map.cpp` | ❌ Codegen Error | Parses OK; `_M_end` symbol not found during codegen |
| `<set>` | `test_std_set.cpp` | ❌ Codegen Error | `__i` auto parameter not resolved in subrange codegen |
| `<ranges>` | `test_std_ranges.cpp` | ❌ Parse Error | Fails on `make_error_code` in `<system_error>` |
| `<iostream>` | `test_std_iostream.cpp` | ❌ Parse Error | Fails on `make_error_code` in `<system_error>` |
| `<chrono>` | `test_std_chrono.cpp` | ❌ Parse Error | Fails on `make_error_code` in `<system_error>` |
| `<atomic>` | N/A | ✅ Compiled | ~6105ms (2026-02-23: Fixed with enum enumerator scope resolution; some static_assert warnings remain) |
| `<new>` | N/A | ✅ Compiled | ~44ms |
| `<exception>` | N/A | ✅ Compiled | ~471ms |
| `<typeinfo>` | N/A | ✅ Compiled | ~41ms (2026-02-05: Fixed with _Complex and __asm support) |
| `<typeindex>` | N/A | ✅ Compiled | ~766ms (2026-02-05: Fixed with _Complex and __asm support) |
| `<numeric>` | N/A | ✅ Compiled | ~884ms (2026-02-13: Compiles successfully) |
| `<variant>` | `test_std_variant.cpp` | ❌ Parse Error | Expected ';' after struct/class definition at variant:1137 |
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
| `<latch>` | `test_std_latch.cpp` | ❌ Codegen Error | ~608ms parse (2026-02-24: improved timing); `_Size` non-type template parameter not resolved during codegen |
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
| `<cmath>` | `test_std_cmath.cpp` | ❌ Codegen Error | ~3440ms parse; codegen errors on fold expressions and static_assert evaluation |

**Legend:** ✅ Compiled | ❌ Failed/Parse/Include Error | ⏱️ Timeout (60s) | 💥 Crash

### Summary (2026-02-24)

**Total headers tested:** 68
**Compiling successfully:** 44 (65%) — note: `<ratio>`, `<utility>`, `<string_view>`, and `<span>` now fail during codegen but previously passed; see notes below
**Parse errors:** 10
**Codegen errors (parsing completes):** 12
**Codegen crash:** 1 (`<string>`)

> **Note:** Several headers that previously compiled now have codegen failures. This is because recent parser improvements allow them to parse more code, which then hits new codegen limitations (e.g., fold expressions, pack expansions, non-type template parameters). The headers `<string_view>` and `<span>` previously compiled because the codegen errors were masked; they still parse in similar times.

### Known Blockers

The most impactful blockers preventing more headers from compiling, ordered by impact:

1. **`<system_error>` ADL-based `make_error_code`**: The pattern `using __adl_only::make_error_code; *this = make_error_code(__e);` fails because FlashCpp doesn't support argument-dependent lookup (ADL) through using declarations. This blocks: `<memory>`, `<ranges>`, `<iostream>`, `<chrono>` and any header that includes `<system_error>`.

2. **Constrained auto parameters (`concept auto __i`) in codegen**: C++20 abbreviated function templates with constrained auto parameters (e.g., `subrange(convertible_to<_It> auto __i, _Sent __s)`) parse but the parameter `__i` is not registered in the symbol table during codegen. Affects: `<algorithm>`, `<tuple>`, `<set>` (via `subrange` in `<ranges_util.h>`).

3. **Fold expression / pack expansion not expanded in codegen**: Some fold expressions and `PackExpansionExprNode`s survive template instantiation and reach codegen, which cannot handle them. Affects: `<string_view>`, `<span>`, `<string>`, `<cmath>`.

4. **Aggregate brace initialization for template types**: `std::array<int, 5> arr = {1, 2, 3, 4, 5}` fails because FlashCpp treats `{}` as constructor lookup rather than aggregate initialization. Affects: `<array>`.

5. **Dependent base class resolution**: Base classes that depend on template parameters (like `__hash_code_base` in `_Hashtable`) are not found during struct definition parsing. Affects: `<functional>` (via `<hashtable.h>`).

6. **Variable template evaluation in constant expressions**: Variable templates like `__is_ratio_v<T>` cannot be evaluated in `static_assert` contexts. Affects: `<ratio>`, `<shared_mutex>`.

7. **Non-type template parameter resolution in codegen**: Symbols like `_Size` or `_M_end` that come from non-type template parameters are not always resolved during code generation. Affects: `<latch>`, `<map>`, `<vector>`.

### Recent Fixes (2026-02-24)

1. **Alias template with namespace-qualified target types**: Fixed deferred instantiation re-parse to handle namespace-qualified names (e.g., `using my_vec = ns1::vec<T>`). Previously, the re-parse only consumed a single identifier, causing `Expected ';'` errors. This unblocked `<vector>` parsing (now reaches codegen).

2. **Qualified member access through base class (`obj.Base::member()`)**: Added handling for `::` after `MemberAccessNode` in the postfix expression parser. The pattern `__x.__ebo_hash::_M_get()` from `hashtable_policy.h` now parses correctly. This unblocked `<functional>` parsing to reach `<hashtable.h>`.

3. **Parent namespace fallback in template and symbol lookup**: Both `resolve_namespace_handle_impl` (symbol table) and `try_instantiate_template` (template registry) now walk up the namespace hierarchy. When inside `std::__cxx11`, looking up `__detail::__to_chars_len` now correctly resolves to `std::__detail::__to_chars_len`. This unblocked `<string>` parsing (now reaches codegen).

4. **Namespace-qualified struct lookup in constexpr evaluator**: Template instantiations registered with short names (e.g., `is_integral$hash`) are now found when the constexpr evaluator looks them up with namespace-qualified names (e.g., `std::is_integral$hash`). Fixed `<type_traits>` compilation (`std::is_integral<int>::value` now evaluates correctly).

5. **Register allocator crash fix**: Added guard against releasing `X64Register::Count` sentinel value. Fixed pre-existing crash in `<limits>` test.

### Recent Fixes (2026-02-23)

1. **Template recursion depth limit increased (10→64)**: The `try_instantiate_template` recursion depth limit was raised from 10 to 64, matching common compiler defaults. This unblocked `<string_view>` and `<span>` which needed deeper template instantiation chains (e.g., `__niter_base`).

2. **Non-type template parameters in deferred base class substitution**: The `name_substitution_map` in `try_instantiate_class_template` now includes non-type template parameters (previously filtered to Type-kind only). This fixes `integral_constant<T,v>::value` codegen when `integral_constant` is instantiated through inheritance chains like `extent_helper<N> : integral_constant<unsigned long long, N>`.

3. **Enum enumerator scope resolution in member functions**: Added lookup of unscoped enum enumerators within the enclosing class scope during code generation. Previously, enumerators like `_Op_clone` from a nested `enum _Op` inside `std::any` were not found in symbol tables during member function codegen. This unblocked `<any>`, `<atomic>`, and partially `<latch>`.

4. **Namespace-scope identifier fallback in codegen**: Added global-qualified fallback lookup for unresolved identifiers during code generation (including parent-namespace lookup and unique namespace-qualified fallback). This resolved `<latch>` failures on `memory_order_relaxed` and `__memory_order_mask` and exposed the remaining `_Size` non-type template substitution gap.

5. **GCC atomic predefined macros**: Added missing predefined macros (`__GCC_ATOMIC_*` and `__GCC_ATOMIC_TEST_AND_SET_TRUEVAL`) in GCC/Clang compatibility mode to match libstdc++ expectations and avoid unresolved macro identifiers during `<atomic>/<latch>` compilation paths.
