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
| `<any>` | `test_std_any.cpp` | ❌ Codegen Error | ~341ms; `_Op_clone` symbol not found during codegen (enum enumerator used as value not substituted) |
| `<utility>` | `test_std_utility.cpp` | ✅ Compiled | ~415ms (2026-02-23: Updated timing; sibling namespace fix confirmed) |
| `<concepts>` | `test_std_concepts.cpp` | ✅ Compiled | ~265ms |
| `<bit>` | N/A | ✅ Compiled | ~328ms (2026-02-06: Fixed with `__attribute__` and type trait whitelist fixes) |
| `<string_view>` | `test_std_string_view.cpp` | ❌ Codegen Error | ~4932ms; parsing completes but codegen fails on `__niter_base` recursion and missing identifiers |
| `<string>` | `test_std_string.cpp` | ❌ Codegen Error | ~1539ms; parsing completes; fails during IR conversion |
| `<array>` | `test_std_array.cpp` | ❌ Codegen Error | ~1040ms; parsing completes but codegen fails on `_Nm` symbol (non-type template param not substituted) |
| `<algorithm>` | `test_std_algorithm.cpp` | ❌ Codegen Error | ~4034ms; parsing completes but codegen fails on template instantiation issues |
| `<span>` | `test_std_span.cpp` | ❌ Codegen Error | ~1479ms; parsing completes but codegen fails on template instantiation issues |
| `<tuple>` | `test_std_tuple.cpp` | ❌ Codegen Error | ~2149ms; parsing completes but codegen fails on template instantiation issues |
| `<vector>` | `test_std_vector.cpp` | ❌ Codegen Error | ~1501ms; parsing completes but codegen fails on template instantiation issues |
| `<memory>` | `test_std_memory.cpp` | ❌ Codegen Error | ~2047ms; parsing completes but codegen fails on template instantiation issues |
| `<functional>` | `test_std_functional.cpp` | ❌ Codegen Error | ~1429ms; parsing completes but codegen fails on template instantiation issues |
| `<map>` | `test_std_map.cpp` | ❌ Codegen Error | ~1450ms; parsing completes but codegen fails on template instantiation issues |
| `<set>` | `test_std_set.cpp` | ❌ Codegen Error | ~1417ms; parsing completes but codegen fails on template instantiation issues |
| `<ranges>` | `test_std_ranges.cpp` | ❌ Codegen Error | ~1742ms; parsing completes; fails during IR conversion |
| `<iostream>` | `test_std_iostream.cpp` | ❌ Codegen Error | ~1688ms; parsing completes; fails during IR conversion |
| `<chrono>` | `test_std_chrono.cpp` | ❌ Codegen Error | ~4298ms; parsing completes but codegen hangs on template instantiation |
| `<atomic>` | N/A | ❌ Codegen Error | ~5523ms (2026-02-13: Parsing completes after member function call shadowing fix; codegen fails on `_Size` symbol) |
| `<new>` | N/A | ✅ Compiled | ~44ms |
| `<exception>` | N/A | ✅ Compiled | ~471ms |
| `<typeinfo>` | N/A | ✅ Compiled | ~41ms (2026-02-05: Fixed with _Complex and __asm support) |
| `<typeindex>` | N/A | ✅ Compiled | ~766ms (2026-02-05: Fixed with _Complex and __asm support) |
| `<numeric>` | N/A | ✅ Compiled | ~884ms (2026-02-13: Compiles successfully) |
| `<variant>` | `test_std_variant.cpp` | ❌ Parse Error | Progressed from line 499→1137; struct body boundary tracking issue |
| `<csetjmp>` | N/A | ✅ Compiled | ~27ms |
| `<csignal>` | N/A | ✅ Compiled | ~101ms (2026-02-13: Now compiles successfully) |
| `<stdfloat>` | N/A | ✅ Compiled | ~14ms (C++23) |
| `<spanstream>` | N/A | ✅ Compiled | ~34ms (C++23) |
| `<print>` | N/A | ✅ Compiled | ~34ms (C++23) |
| `<expected>` | N/A | ✅ Compiled | ~33ms (C++23) |
| `<text_encoding>` | N/A | ✅ Compiled | ~34ms (C++26) |
| `<stacktrace>` | N/A | ✅ Compiled | ~35ms (C++23) |
| `<barrier>` | N/A | ❌ Codegen Error | ~2141ms; parsing completes but codegen fails (depends on `<atomic>`) |
| `<coroutine>` | N/A | ❌ Parse Error | ~31ms; fails on coroutine-specific syntax |
| `<latch>` | N/A | ❌ Codegen Error | ~4598ms; parsing completes; fails on `_Size` symbol (non-type template param not substituted) |
| `<shared_mutex>` | N/A | ❌ Codegen Error | ~744ms; parsing completes; fails on `_S_epoch_diff` symbol |
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
**Compiling successfully:** 42 (62%)
**Parse errors:** 2 (`<variant>`, `<coroutine>`)
**Codegen errors (parsing completes):** 23
**Timeout:** 1 (`<cmath>`)

### Known Blockers

The most impactful blockers preventing more headers from compiling (parsing succeeds but codegen fails):

1. **Non-type template parameter substitution in codegen**: Template parameters like `_Nm` in `std::array<T, _Nm>` or `_Size` in atomic types are not being substituted in generated code. This causes symbol-not-found errors for `_Nm`, `_Size`, `_S_epoch_diff`, `_Op_clone`, etc. Affects: `<array>`, `<atomic>`, `<latch>`, `<any>`, `<shared_mutex>`.

2. **`integral_constant<T,v>::value` codegen**: The static member initializer for `value` in `std::integral_constant` instantiations is not recognized as an expression during codegen. Results in zero-initialization warnings. Non-fatal but causes incorrect runtime behavior for type traits.

3. **Template recursion depth limit**: The current limit of 10 is too low for some standard library templates like `__niter_base`. Affects: `<string_view>`, `<algorithm>`, `<ranges>`.

4. **Fold expression codegen**: Fold expressions that reach codegen haven't been expanded during template instantiation. Affects: `<tuple>`, `<array>`.

### Recent Fixes (2026-02-23)

1. **Namespace-qualified type alias resolution in nested namespaces**: `resolve_namespace_handle_impl` now tries resolving relative to the current namespace before falling back to global. E.g., inside `namespace outer`, `inner::type` now correctly resolves to `outer::inner::type`. This was blocking `<compare>` (where `inner::type(__v)` functional casts failed inside sibling namespaces) and `<utility>`.

2. **Dependent name validation in template bodies**: When inside a template body (`parsing_template_body_` or `struct_parsing_context_stack_`), qualified names like `pointer::pointer_to()` (where `pointer` is a template-dependent type alias from `using pointer = _Ptr;`) are now accepted as forward declarations instead of erroring. Also added fallback to `gSymbolTable.lookup()` for scope-local type aliases. This unblocked `<optional>` and the `ptr_traits.h` header used by many containers.

3. **Bitfield width parsing with default member initializers**: Changed bitfield width expression parsing from `DEFAULT_PRECEDENCE` (2) to precedence 4 (above assignment=3) so that `unsigned _M_msb:1 = 0;` correctly parses the width as `1` and the `= 0` as a default member initializer. Fixes parsing of `max_size_type.h` used by `<string_view>` and `<ranges>`.

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

