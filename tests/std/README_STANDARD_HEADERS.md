# Standard Header Tests

This directory contains test files for C++ standard library headers to assess FlashCpp's compatibility with the C++ standard library.

## Current Status

| Header | Test File | Status | Notes |
|--------|-----------|--------|-------|
| `<limits>` | `test_std_limits.cpp` | ✅ Compiled | ~1369ms |
| `<type_traits>` | `test_std_type_traits.cpp` | ❌ Semantic Error | ~1142ms (retested 2026-04-20). Header parsing succeeds, but the targeted test fails `static_assert(std::is_integral<int>::value)`. Distilled expected-failure repro: `tests/test_std_type_traits_is_integral_any_of_fail.cpp`. |
| `<compare>` | `test_std_compare_ret42.cpp` | ❌ Codegen Error | ~609ms (retested 2026-04-11). Targeted test still compiles, but bare `#include <compare>` now fails with "Ambiguous constructor call" during codegen of a namespace-level node. |
| `<version>` | `test_std_version.cpp` | ✅ Compiled | ~41ms |
| `<source_location>` | `test_std_source_location.cpp` | ✅ Compiled | ~41ms |
| `<numbers>` | N/A | ✅ Compiled | ~510ms |
| `<initializer_list>` | N/A | ✅ Compiled | ~32ms. Direct `std::initializer_list<T> values = {...}` object list-initialization is now covered by `tests/test_std_initializer_list_direct_brace_ret0.cpp` (retested 2026-04-20). |
| `<ratio>` | `test_std_ratio.cpp` | ✅ Compiled | ~639ms. The header still compiles, but `std::ratio_less` remains blocked because non-type default template arguments that depend on qualified constexpr members (for example `__ratio_less_impl`'s bool defaults) are still not fully instantiated/evaluated. |
| `<optional>` | `test_std_optional.cpp` | ❌ Parse Error | ~2861ms (retested 2026-04-20). Earlier MSVC `<type_traits>` parser stops are fixed; current first hard error is MSVC `<utility>:82` at a parenthesized function-template declaration with "Expected '(' for parameter list". **2026-04-23 (Linux/libstdc++):** the long-standing `Expected identifier token` parse stops at `_Optional_payload_base::_Storage`'s templated constructor (`<optional>:209`) and partial-specialization destructor (`<optional>:259`) are now fixed. On Linux, `<optional>` parses completely and fails later in codegen on deferred `_Optional_base` / `_Optional_payload` placeholder resolution. Regression: `tests/test_nested_member_template_ctor_dtor_ret0.cpp`. |
| `<any>` | `test_std_any.cpp` | ❌ Codegen Error | ~607ms (retested 2026-04-11). Targeted test now fails with "Expected symbol '_Arg' to exist in code generation" in `std::any` constructor. |
| `<utility>` | `test_std_utility.cpp` | ❌ Codegen Error | ~830ms (retested 2026-04-11). Targeted test now fails with codegen errors from template deduction / Non-type parameter issues. |
| `<concepts>` | `test_std_concepts.cpp` | ✅ Compiled | ~1518ms (retested 2026-04-20). The line 254 requires-expression pack expansion blocker is fixed by `tests/test_std_concepts_pack_expansion_ret42.cpp`. The compile still logs recoverable `is_integral_v` instantiation warnings, tracked separately under `<type_traits>`. |
| `<bit>` | `test_std_bit.cpp` | ✅ Compiled | ~625ms |
| `<string_view>` | `test_std_string_view.cpp` | ❌ Compile Error | ~1460ms (retested 2026-04-11). Call to deleted function 'swap' in `stl_pair.h:308`. Blocked by eager inline member body parsing during implicit template class instantiation (std::pair::swap tries to swap const members). |
| `<string>` | `test_std_string.cpp` | ❌ Compile Error | ~2192ms (retested 2026-04-11). Call to deleted function 'swap' — same `stl_pair.h:308` blocker as `<string_view>`. |
| `<array>` | `test_std_array.cpp` | ❌ Parse Error | ~2554ms (retested 2026-04-20). Earlier MSVC `<type_traits>` parser stops are fixed; current first hard error is MSVC `<utility>:82` at a parenthesized function-template declaration with "Expected '(' for parameter list". |
| `<algorithm>` | `test_std_algorithm.cpp` | ❌ Parse Error | ~3882ms (retested 2026-04-20). The simple std-style `(max)` / `(min)` focused regression now passes (`tests/test_std_algorithm_max_ret3.cpp`), but the real header now stops earlier in MSVC `<utility>:82` on parenthesized function-template declaration parsing. |
| `<span>` | `test_std_span.cpp` | ✅ Compiled | ~41ms (retested 2026-04-11). **NEW: Now compiles successfully!** Previous iterator/ranges codegen blockers are resolved. |
| `<tuple>` | `test_std_tuple.cpp` | ❌ Compile Error | ~1564ms (retested 2026-04-11). "unsupported PackExpansionExprNode reached semantic analysis". |
| `<vector>` | `test_std_vector.cpp` | ❌ Compile Error | ~2337ms (retested 2026-04-20). Still stops in UCRT with "No matching function for call to '__stdio_common_vfwprintf'" during overload resolution; not a crash. Distilled expected-failure repro: `tests/test_ucrt_vfwprintf_const_pointer_alias_arg_fail.cpp`. |
| `<deque>` | `test_std_deque.cpp` | 💥 Crash | ~2464ms (retested 2026-04-11). |
| `<list>` | `test_std_list.cpp` | 💥 Crash | ~1987ms (retested 2026-04-11). |
| `<queue>` | `test_std_queue.cpp` | 💥 Crash | ~2522ms (retested 2026-04-11). |
| `<stack>` | `test_std_stack.cpp` | 💥 Crash | ~2464ms (retested 2026-04-11). |
| `<memory>` | `test_std_memory.cpp` | 💥 Crash | ~5108ms (retested 2026-04-11). |
| `<functional>` | `test_std_functional.cpp` | ❌ Parse Error | ~3723ms (retested 2026-04-20). Earlier MSVC `<type_traits>` parser stops are fixed; current first hard error is MSVC `<utility>:82` at a parenthesized function-template declaration with "Expected '(' for parameter list". |
| `<map>` | `test_std_map.cpp` | ❌ Parse Error | ~2988ms (retested 2026-04-20). Earlier MSVC `<type_traits>` parser stops are fixed; current first hard error is MSVC `<utility>:82` at a parenthesized function-template declaration with "Expected '(' for parameter list". |
| `<set>` | `test_std_set.cpp` | ❌ Compile Error | ~2350ms (retested 2026-04-12). The earlier variable-template/type-traits arity blocker is gone. Current first error is later in the Windows UCRT headers: "No matching function for call to '__stdio_common_vfwprintf'". |
| `<ranges>` | `test_std_ranges.cpp` | ❌ Compile Error | ~2906ms (retested 2026-04-12). The earlier variable-template/type-traits arity blocker is gone. Current first error is later in the Windows UCRT headers: "No matching function for call to '__stdio_common_vfwprintf'". |
| `<iostream>` | `test_std_iostream.cpp` | 💥 Crash | ~4559ms (retested 2026-04-11). |
| `<sstream>` | `test_std_sstream.cpp` | 💥 Crash | ~4565ms (retested 2026-04-11). |
| `<fstream>` | `test_std_fstream.cpp` | 💥 Crash | ~4642ms (retested 2026-04-11). |
| `<chrono>` | `test_std_chrono.cpp` | ❌ Compile Error | ~6638ms (retested 2026-04-11). Call to deleted function 'swap'. |
| `<atomic>` | `test_std_atomic.cpp` | ✅ Compiled | ~838ms (retested 2026-04-24, Linux/libstdc++). **NEW: Now compiles successfully on Linux!** Previous deferred member function codegen errors are resolved. |
| `<new>` | `test_std_new.cpp` | ✅ Compiled | ~56ms |
| `<exception>` | `test_std_exception.cpp` | ✅ Compiled | ~368ms (retested 2026-04-24, Linux/libstdc++). **NEW: Now compiles successfully on Linux!** The `exception_ptr` copy-vs-move-constructor ambiguity is resolved by the rvalue overload-rank fix. Regression: `tests/test_rvalue_ref_overload_preference_ret0.cpp`. |
| `<stdexcept>` | `test_std_stdexcept.cpp` | 💥 Crash | ~4390ms (retested 2026-04-11). |
| `<typeinfo>` | N/A | ✅ Compiled | ~54ms |
| `<typeindex>` | N/A | ❌ Codegen Error | ~640ms (retested 2026-04-11). "Cannot use copy initialization with explicit constructor". |
| `<numeric>` | `test_std_numeric.cpp` | ❌ Codegen Error | ~2299ms (retested 2026-04-11). Targeted test now fails with codegen errors. |
| `<iterator>` | `test_std_iterator.cpp` | ❌ Compile Error | ~2481ms (retested 2026-04-11). Call to deleted function 'swap'. |
| `<variant>` | `test_std_variant.cpp` | ✅ Compiled | ~736ms (retested 2026-04-24, Linux/libstdc++). **NEW: Now compiles successfully on Linux!** The `_Variadic_union` arithmetic non-type template argument (`_Np-1`) inside a member initializer is now resolved. |
| `<csetjmp>` | N/A | ✅ Compiled | ~35ms |
| `<csignal>` | N/A | ✅ Compiled | ~140ms |
| `<stdfloat>` | N/A | ✅ Compiled | ~16ms (C++23) |
| `<spanstream>` | N/A | ✅ Compiled | ~44ms (C++23) |
| `<print>` | N/A | ✅ Compiled | ~52ms (C++23) |
| `<expected>` | N/A | ✅ Compiled | ~62ms (C++23) |
| `<text_encoding>` | N/A | ✅ Compiled | ~45ms (C++26) |
| `<stacktrace>` | N/A | ✅ Compiled | ~47ms (C++23) |
| `<barrier>` | N/A | 💥 Crash | ~5458ms. Stack overflow during template instantiation |
| `<coroutine>` | N/A | ❌ Parse Error | ~36ms. Requires `-fcoroutines` flag |
| `<latch>` | `test_std_latch.cpp` | ✅ Compiled | ~832ms (retested 2026-04-24, Linux/libstdc++). **NEW: Now compiles successfully on Linux!** Prior "sema missed return conversion (int → long)" errors in latch member functions are resolved. |
| `<shared_mutex>` | `test_std_shared_mutex.cpp` | ❌ Codegen Error | ~2733ms (retested 2026-04-11). "Ambiguous constructor call for 'std::chrono::time_point'". |
| `<cstdlib>` | N/A | ✅ Compiled | ~120ms |
| `<cstdio>` | N/A | ✅ Compiled | ~70ms |
| `<cstring>` | N/A | ✅ Compiled | ~64ms |
| `<cctype>` | N/A | ✅ Compiled | ~110ms |
| `<cwchar>` | N/A | ✅ Compiled | ~66ms |
| `<cwctype>` | N/A | ✅ Compiled | ~360ms |
| `<cerrno>` | N/A | ✅ Compiled | ~32ms |
| `<cassert>` | N/A | ✅ Compiled | ~31ms |
| `<cstdarg>` | N/A | ✅ Compiled | ~31ms |
| `<cstddef>` | N/A | ✅ Compiled | ~56ms |
| `<cstdint>` | N/A | ✅ Compiled | ~35ms |
| `<cinttypes>` | N/A | ✅ Compiled | ~75ms |
| `<cuchar>` | N/A | ✅ Compiled | ~78ms |
| `<cfenv>` | N/A | ✅ Compiled | ~44ms |
| `<clocale>` | N/A | ✅ Compiled | ~36ms |
| `<ctime>` | N/A | ✅ Compiled | ~58ms |
| `<climits>` | N/A | ✅ Compiled | ~30ms |
| `<cfloat>` | N/A | ✅ Compiled | ~32ms |
| `<cmath>` | `test_std_cmath.cpp` | ❌ Compile Error | ~6327ms (retested 2026-04-11). "Operator- not defined for operand types". |
| `<system_error>` | N/A | 💥 Crash | ~4400ms (retested 2026-04-11). |
| `<scoped_allocator>` | N/A | ❌ Compile Error | ~1868ms (retested 2026-04-11). "unsupported PackExpansionExprNode". |
| `<charconv>` | N/A | ✅ Compiled | ~930ms |
| `<numbers>` | N/A | ✅ Compiled | ~510ms |
| `<mdspan>` | N/A | ❌ Compile Error | ~12ms (retested 2026-04-11). |
| `<flat_map>` | N/A | ❌ Compile Error | ~12ms (retested 2026-04-11). |
| `<flat_set>` | N/A | ❌ Compile Error | ~13ms (retested 2026-04-11). |
| `<unordered_set>` | N/A | ❌ Compile Error | ~2801ms (retested 2026-04-11). Call to deleted function 'swap'. |
| `<unordered_map>` | N/A | ❌ Compile Error | ~2801ms (retested 2026-04-11). Call to deleted function 'swap'. |
| `<mutex>` | N/A | ❌ Compile Error | ~3690ms (retested 2026-04-11). "unsupported PackExpansionExprNode" — previously was a parse error, now gets further. |
| `<condition_variable>` | N/A | ❌ Compile Error | ~5581ms (retested 2026-04-11). Call to deleted function 'swap' — previously was a crash, now parses successfully. |
| `<thread>` | N/A | ❌ Compile Error | ~2801ms (retested 2026-04-11). Call to deleted function 'swap' — previously was a parse error, now parses successfully. |
| `<semaphore>` | N/A | ❌ Codegen Error | ~3207ms (retested 2026-04-11). "Ambiguous constructor call for 'std::chrono::time_point'" — previously was a parse error, now parses successfully. |
| `<stop_token>` | N/A | 💥 Crash | ~6254ms (retested 2026-04-11). |
| `<bitset>` | N/A | 💥 Crash | ~4850ms (retested 2026-04-11). |
| `<execution>` | N/A | ❌ Compile Error | ~3331ms (retested 2026-04-11). Call to deleted function 'swap' — previously was a parse error, now parses successfully. |
| `<generator>` | N/A | ❌ Compile Error | ~2593ms (retested 2026-04-11). Call to deleted function 'swap' — previously was a parse error, now parses successfully. (C++23) |

**Legend:** ✅ Compiled | ❌ Failed/Parse/Include Error | 💥 Crash

#### 2026-04-24 Targeted Retests (Linux/libstdc++)

Rebuilt `x64/Sharded/FlashCpp` on Linux with clang++ after applying CliPPy's rvalue-reference overload-resolution fix (`exception_ptr` move-ctor preference) and template instantiation depth guard. Ran each std header test against system libstdc++-14 headers.

New changes vs 2026-04-23 sweep:
- **NEW ✅ `<exception>`**: Now compiles cleanly (~368ms). The `exception_ptr` copy-vs-move-constructor ambiguity in `operator=` is resolved by the rvalue overload-rank fix. Regression: `tests/test_rvalue_ref_overload_preference_ret0.cpp`.
- **NEW ✅ `<atomic>`**: Now compiles cleanly (~838ms). Previous deferred member function codegen errors are gone.
- **NEW ✅ `<latch>`**: Now compiles cleanly (~832ms). Prior "sema missed return conversion (int → long)" errors are resolved.
- **NEW ✅ `<variant>`**: Now compiles cleanly (~736ms). The `_Variadic_union` member-initializer arithmetic non-type argument blocker (`_Np-1`) is resolved.

Linux sweep summary (2026-04-24) for the std header files under `tests/std/test_std_*.cpp`:
- Compiles cleanly: `<atomic>`, `<bit>`, `<compare>`, `<concepts>`, `<exception>`, `<latch>`, `<limits>`, `<new>`, `<source_location>`, `<span>`, `<variant>`, `<version>`, plus small focused regressions (`test_std_pair_swap_deleted_member`, `test_std_typeinfo_ret0`).
- Compile/codegen errors (rc=1, no crash): `<any>` (Ambiguous constructor call), `<numeric>` (unresolved auto type mangling), `<optional>` (unresolved auto type mangling), `<ratio>` (static_assert: ratio_less value), `<shared_mutex>` (Ambiguous constructor call), `<type_traits>` (static_assert failed), `<utility>` (Operator< not defined).
- Deep-template crashes (rc=139, SIGSEGV from stack overflow at `try_instantiate_class_template` depth ~57): `<algorithm>`, `<array>`, `<chrono>`, `<cmath>`, `<deque>`, `<fstream>`, `<functional>`, `<iostream>`, `<iterator>`, `<list>`, `<map>`, `<memory>`, `<queue>`, `<ranges>`, `<set>`, `<sstream>`, `<stack>`, `<stdexcept>`, `<string>`, `<string_view>`, `<tuple>`, `<vector>`. The depth guard (MAX_INSTANTIATION_NESTING_DEPTH=256) prevents unbounded recursion from libstdc++'s `__convertible_from`/`__normal_iterator` mutual dependency, but cannot prevent the crash that occurs within the 57th frame's work when the call-chain depth exhausts the 16MB thread stack. The depth guard does prevent a full stack overflow to 300+ levels. Regression: `tests/test_template_recursive_instantiation_depth_ret0.cpp`.

#### 2026-04-23 Targeted Retests (Linux/libstdc++)

Rebuilt `x64/Sharded/FlashCpp` on Linux with clang++ and ran each std header test directly against the system libstdc++-14 headers. Full regression suite (`tests/run_all_tests.sh`, 2187 tests) continues to pass. Linux-only observations:

- `<optional>` no longer stops on the nested-member-class-template constructor/destructor parse gap at `<optional>:209` / `<optional>:259`. Header now parses completely (~750ms) and fails later in codegen on deferred `_Optional_base` / `_Optional_payload` placeholder type resolution. See the 2026-04-23 fix entry below.
- The generic blocker (member class templates whose body contains a template constructor or a destructor) was shared by other headers on Linux. `<variant>` now parses further and stops with a new first error at `<variant>:418` in `_Variadic_union(in_place_index_t<_Np>, _Args&&...) : _M_rest(in_place_index<_Np-1>, ...)`, which is an arithmetic non-type template argument (`_Np-1`) inside a member initializer — a separate issue.
- Linux sweep summary (current state) for the std header files under `tests/std/test_std_*.cpp`:
  - Compiles cleanly: `<bit>`, `<compare>`, `<concepts>`, `<limits>`, `<new>`, `<source_location>`, `<span>`, `<version>`, plus small focused regressions (`test_std_pair_swap_deleted_member`, `test_std_typeinfo_ret0`).
  - Parse-phase errors (rc=1, no crash): `<any>`, `<atomic>`, `<exception>`, `<latch>`, `<numeric>`, `<optional>`, `<ratio>`, `<shared_mutex>`, `<type_traits>`, `<utility>`, `<variant>`.
  - Deep-template crashes (rc=139) remain in many heavier headers — Linux libstdc++ paths diverge from MSVC, so the MSVC-focused table above is not a direct substitute. These need their own Linux investigation.

#### 2026-04-20 Targeted Retests

- Rebuilt `x64\Sharded\FlashCppMSVC.exe` and re-ran the newest focused regressions: `tests/test_static_member_pack_expansion_boundary_ret0.cpp`, `tests/test_constexpr_comma_operator_ret0.cpp`, `tests/test_std_initializer_list_direct_brace_ret0.cpp`, `tests/test_std_concepts_pack_expansion_ret42.cpp`, and `tests/test_std_algorithm_max_ret3.cpp`; all passed.
- `<concepts>` now compiles. The previous line 254 blocker, a pack expansion inside a requires expression, is covered by `tests/test_std_concepts_pack_expansion_ret42.cpp`.
- `<type_traits>` still parses but fails semantically at `static_assert(std::is_integral<int>::value)`. The distilled expected-failure repro is `tests/std/test_std_type_traits_is_integral_any_of_fail.cpp`: variable-template partial specialization feeds another variable template, then that value is used as a non-type base-template argument.
- `<array>`, `<optional>`, `<variant>`, `<functional>`, `<map>`, and `<algorithm>` now share a higher-impact parse blocker in MSVC `<utility>:82`: declarations like `constexpr Ty(max)(initializer_list<Ty>, Predicate);` are rejected with "Expected '(' for parameter list". The isolated expected-failure repro is `tests/std/test_std_utility_parenthesized_func_template_decl_fail.cpp`.
- `<vector>` still stops in UCRT overload resolution on `__stdio_common_vfwprintf`; the targeted retest confirms this is not a crash. The distilled expected-failure repro is `tests/std/test_ucrt_vfwprintf_const_pointer_alias_arg_fail.cpp`, which reduces the failure to passing `_locale_t const` to `_locale_t` where `_locale_t` is a pointer alias.

#### 2026-04-19 Fix: nullptr literal support for void* parameters

Fixed `nullptr` literal matching for `void*` and `const void*` parameters. Regression test: `tests/test_nullptr_void_ptr_arg_ret0.cpp`.

### Summary (older full sweep; targeted updates through 2026-04-20)

**Total headers tested:** 96
**Compiling successfully (parse + codegen):** 55 (57%)
**Codegen errors (parsing succeeds but codegen fails):** 18 (including many headers now failing with Itanium mangling issues after alloc_traits.h fix unblocked parsing)
**Parse errors:** 10
**Crashes:** 4 in the last full sweep (deep template-instantiation paths; see the targeted retest note below for newer per-header updates)

The aggregate counts above are still from the older full sweep. The table and dated retest notes include newer targeted updates, but the counts should not be treated as refreshed until `tests/std/test_standard_headers.ps1` or an equivalent full sweep is rerun without the temporary early-stop limit.

#### 2026-04-08 Retests

- `<span>` was re-checked after tightening self-referential template member-operator parameter matching so overload lookup no longer keys off raw pattern layout state. The new regression tests cover pointer-member and mixed-member template layouts, but the real `<span>` repro still fails later in codegen on the same iterator/ranges follow-ons (`move` / `fill_n`, `_Extent`, `__max_size_type`, final `Operator-`), so the table entry stays in the codegen-error bucket with the updated timing above.

#### 2026-04-09 Retests

- An earlier fix for dependent `decltype(swap(...))` probes (see `tests/test_dependent_swap_decltype_noexcept_ret0.cpp`) moved several headers past a `bits/move.h` blocker, exposing a new failure in `<concepts>`.
- This PR resolves the `<concepts>` failure by adding leading-cv recovery for deferred alias templates (see `tests/test_alias_template_const_deferred_ref_ret0.cpp`). The affected headers (`<memory>`, `<functional>`, `<deque>`, `<set>`) now fail later in `bits/stl_pair.h`.

#### 2026-04-10 Retests

- Namespaced implicit template instantiations now register lazy member-function stubs before replaying bodies, and member-call resolution no longer forces incomplete dependent placeholder types down the concrete-class path. The focused std-style regression `tests/std/test_std_pair_swap_deleted_member.cpp` now compiles in ~20ms.
- Re-checking `<memory>` (~1.63s), `<functional>` (~1.17s), `<deque>` (~0.93s), and `<set>` (~0.93s) shows that the old simplest `pair::swap` unused-body case is gone, but real libstdc++ still reaches a deeper deleted-`swap` path at `bits/stl_pair.h:308`, so those headers stay in the compile-error bucket for now.

#### 2026-04-11 Retests

- Function parameter parsing now preserves reference-to-array declarators instead of decaying them to pointers, and function-template deduction can bind non-type array extents from real call arguments. The new focused regressions `tests/test_array_ref_nontype_deduction_ret0.cpp` and `tests/test_array_enable_if_deduction_ret0.cpp` both pass alongside the older swap-SFINAE coverage.
- Re-checking `<set>` (~1.48s), `<functional>` (~1.77s), and `<memory>` (~2.32s) shows that the focused array-bound deduction gap is covered, but the real libstdc++ `pair::swap` instantiations still fail later in `bits/stl_pair.h:308`, so those headers remain in the compile-error bucket for now.
- Function-template instantiation now shares dependent default-template-argument recovery between explicit-argument and call-argument deduction paths, including dependent type aliases/conditionals and dependent non-type defaults such as `sizeof(T)`. The new focused regressions `tests/test_func_template_dependent_default_alias_ret0.cpp`, `tests/test_func_template_dependent_default_conditional_ret0.cpp`, and `tests/test_func_template_dependent_default_nontype_sizeof_ret0.cpp` all pass.
- Re-checking `<set>` (~2.27s), `<functional>` (~2.77s), `<deque>` (~2.97s), and `<memory>` (~4.85s) shows that the old `alloc_traits.h:904` `__make_move_if_noexcept_iterator` stop is gone. `<set>` and `<functional>` now fail later at `bits/node_handle.h:285` on deleted `swap(_M_pkey, __nh._M_pkey)`, while `<deque>` and `<memory>` run deeper and currently crash after repeated zero-size dependent `sizeof` static-assert fallout (`__is_integer_nonstrict::__value` / allocator-style completeness checks).

#### 2026-04-12 Retests

- Variable-template primary instantiation now canonicalizes explicit arguments with parameter packs and defaults before substitution, and primary variable-template initializers now go through the generic pack-aware template substitutor instead of the old fixed-arity map path. The new focused regressions `tests/test_var_template_variadic_primary_ret42.cpp` and `tests/test_var_template_default_dependent_primary_ret42.cpp` both pass alongside the older variable-template coverage.
- Re-checking `<array>` (~2.27s), `<optional>` (~2.24s), `<variant>` (~2.66s), `<functional>` (~3.27s), and `<map>` (~2.75s) shows that the old `_Is_any_of_v` / "Template argument count mismatch: expected 2, got 17" stop is gone. These headers now reach a later parser gap in MSVC `<type_traits>` at `struct is_nothrow_invocable_r : _Select_invoke_traits<_Callable, _Args...>::template _Is_nothrow_invocable_r<_Rx>`.
- Re-checking `<set>` (~2.35s) and `<ranges>` (~2.91s) shows that the same variable-template blocker is gone there too. Both headers now get further into the Windows UCRT path and currently stop on `__stdio_common_vfwprintf` overload resolution instead of the earlier template-arity failure.

#### 2026-04-14 Retests

- Base-specifier parsing/resolution now accepts dependent member-template chains after a template-id (`Owner<Args...>::template Member<T>`), and deferred template-base instantiation preserves/materializes those member template-ids instead of dropping them to plain `::name` chains. Focused regression: `tests/test_dependent_member_template_base_alias_ret42.cpp`.
- Template-argument parsing now accepts bare non-member function types with a calling convention before the parameter list, plus trailing cv/ref/noexcept qualifiers, so partial specializations like `_Function_args<_Ret __cdecl(_Types...) noexcept>` no longer stop during pattern parsing. Focused regression: `tests/test_nonmember_callconv_function_partial_spec_ret42.cpp`.
- Re-checking `<array>` (~2.12s), `<optional>` (~2.24s), `<variant>` (~2.53s), `<functional>` (~3.01s), and `<map>` (~2.48s) shows that both earlier `type_traits` parser gaps are gone. These headers now reach a later member-function declaration parser stop in `type_traits:2378-2379` at `less::operator() const noexcept(...)`.

#### 2026-04-18 Retests

- **Global namespace prefix in expression contexts**: Fixed by template argument parser lookahead improvements. The focused test `tests/test_noexcept_complex_expr_ret0.cpp` now compiles successfully with the current Sharded build.
- **Added `_is_convertible_to` builtin type trait**: MSVC uses this in `<concepts>` line 38 and `<type_traits>`. Adding it to the trait map fixes the parser error at these locations. Test: `tests/test_volatile_qualified_conv_ret0.cpp`.
- `<concepts>` now passes line 38 but hits new blocker at line 254 (pack expansion in requires expression). Superseded by the 2026-04-20 retest: that blocker is now fixed and the header compiles.
- `<type_traits>` now parses past the earlier errors but fails on semantic (is_integral static_assert).

#### 2026-04-07 Retests

- `<atomic>` and `<latch>` were re-checked after teaching sema/parser to preserve user-defined enum identity for overloaded binary operators and parenthesized functional casts. That clears the old `memory_order | __memory_order_modifier(...)` semantic stop from `bits/atomic_base.h`, so both headers now progress into later atomic-wait codegen fallout instead of failing during scoped-enum checking. `<atomic>` currently stops on the same deeper `_M_do_wait` / missing default-argument / dependent-payload-size / `memory_order_seq_cst` symbol issues summarized in the table, while `<latch>` now fails even later on `_M_do_wait`, `__mutex_base` constructor recovery, integer-conversion gaps in wait helpers, and missing `memory_order_relaxed` symbol recovery.
- `<span>`, `<array>`, and `<string_view>` were also re-checked after fixing namespace-scope post-`struct` brace initialization to preserve real `{}` initializers and after enforcing the standard minimum complete-object size for empty structs. That removes the bogus `_Synth3way` / ranges-CPO copy-initialization failure that had started appearing once earlier fixes unblocked more of libstdc++, but all three headers still fail later in the pipeline on the follow-ons summarized in the table.

#### Earlier targeted retests still current

- 2026-04-06: `<tuple>`, `<string>`, `<ranges>`, and `<stdexcept>`
- 2026-04-05: `<ratio>`, `<chrono>`, and `<shared_mutex>`
- 2026-04-04: `<optional>` and `<variant>` history before the newer 2026-04-20 targeted retest; `<algorithm>` and `<vector>` have newer targeted results in the table.

The overall header counts above still reflect the older full sweep and need a future comprehensive rerun before they are updated.

### Recommended Next Work

1. **Fix parenthesized function-template declarations.** This is the best next std-header include blocker because it is now the first hard error for `<array>`, `<optional>`, `<variant>`, `<functional>`, `<map>`, and `<algorithm>` on the current MSVC 14.44 headers. The isolated expected-failure test is `tests/test_std_utility_parenthesized_func_template_decl_fail.cpp`; it reproduces the MSVC `<utility>:82` shape without including `<utility>`.

   Target pattern:

   ```cpp
   template <class Ty, class Predicate>
   constexpr Ty(max)(initializer_list<Ty>, Predicate);
   ```

   When this is fixed, rename the repro from `_fail.cpp` to a `_ret0.cpp` test and re-run the affected headers above.

2. **Then fix variable-template values used as base-class non-type arguments.** This is the next semantic blocker for `<type_traits>` and a source of recoverable warnings in `<concepts>`. The distilled expected-failure test is `tests/test_std_type_traits_is_integral_any_of_fail.cpp`; it reduces the MSVC `is_integral` failure to `is_same_v<T, T>` feeding `is_integral_v<T>`, which then feeds `bool_constant<is_integral_v<T>>`.

3. **Fix top-level const on pointer aliases during overload resolution.** This is the isolated UCRT blocker behind `<vector>`'s `__stdio_common_vfwprintf` failure. The distilled expected-failure test is `tests/test_ucrt_vfwprintf_const_pointer_alias_arg_fail.cpp`; it reduces the real `_locale_t const _Locale` wrapper argument to a call that expects `_locale_t`, where `_locale_t` aliases `void*`.

### Known Blockers

The most impactful blockers preventing more headers from compiling, ordered by impact:

As of the 2026-04-20 targeted retest, the global namespace prefix blocker and the `<concepts>` requires-expression pack expansion blocker are fixed. The earlier `Template<...>::template Member<...>` base lookup gap and the bare non-member calling-convention function-type partial-specialization gap are also fixed.

1. **Parenthesized function-template declarations**: MSVC `<utility>` declares overloads such as `constexpr Ty(max)(initializer_list<Ty>, Predicate);`. FlashCpp currently rejects this with "Expected '(' for parameter list". This is the highest-impact include blocker because it is the first hard error for `<array>`, `<optional>`, `<variant>`, `<functional>`, `<map>`, and `<algorithm>`. Isolated repro: `tests/test_std_utility_parenthesized_func_template_decl_fail.cpp`.

2. **Variable-template values used as base-class non-type arguments**: `<type_traits>` parses, but `std::is_integral<int>::value` evaluates false in the targeted test. The distilled pattern is `is_same_v<T, T>` feeding `is_integral_v<T>`, then `bool_constant<is_integral_v<T>>` inheritance. Isolated repro: `tests/test_std_type_traits_is_integral_any_of_fail.cpp`.

3. **Top-level const on pointer aliases during overload resolution**: UCRT inline wrappers pass `_locale_t const` to declarations expecting `_locale_t`, where `_locale_t` is a pointer alias. FlashCpp currently treats that top-level const as call-incompatible and rejects `__stdio_common_vfwprintf`. Isolated repro: `tests/test_ucrt_vfwprintf_const_pointer_alias_arg_fail.cpp`.

4. **Deferred template-base placeholder materialization / inherited-member follow-ons**: Some dependent base arguments now materialize correctly, and chained member access no longer immediately erases concrete payload types back to `type_index=0`, but later CRTP/deferred-body codegen still has remaining gaps where instantiated payload structs are not always recovered as full structs and inherited members are not fully reconstructed. `<optional>` now reaches the later `_Optional_payload<...>` / `_M_engaged` failures with concrete type index `2758`, but those deferred paths still lose struct info or inherited `_M_engaged` lookup during codegen.

5. **Late atomic implementation follow-ons after the scoped-enum overload/cast fix**: Pointer-style `__atomic_add_fetch` / `__atomic_fetch_sub`, namespace-qualified explicit function-template calls such as `std::__atomic_impl::__compare_exchange<_AtomicRef>(...)`, `__builtin_memcpy`, and the earlier `memory_order | __memory_order_modifier(...)` scoped-enum stop now all get past parsing/sema. `<atomic>` still fails later in codegen on `_M_do_wait`, missing default-argument recovery, dependent `__compare_exchange` payload sizing, and `memory_order_seq_cst` symbol recovery; `<latch>` now fails later in the same stack on `_M_do_wait`, `__mutex_base` constructor recovery, integer-conversion gaps, and `memory_order_relaxed` symbol recovery. Affects: `<atomic>`, `<latch>`.

6. **Late chrono/time-point semantic follow-ons after the statement-disambiguation fix**: `<chrono>` no longer stops first on `duration::operator+=` / `operator-=` being misparsed as declarations when the namespace-scope alias `__r` shadows the member name. The next blocker is deeper template/sema handling around `chrono::time_point`, including a non-dependent-name `__time_point` error and repeated ratio/time-point static-assert fallout; `<shared_mutex>` now rides that stack further and then fails later in codegen on `_S_epoch_diff`. Affects: `<chrono>`, `<shared_mutex>`.

7. **Iterator / ranges downstream follow-on failures after the latest operator fixes**: The simple free-operator-template gap is fixed, but libstdc++ headers still hit later failures around iterator arithmetic / comparisons (`Operator-`, `Operator!=`, `_S_empty`, `_S_size`), `make_move_iterator`, and missing struct type info for some helper types. Affects: `<string_view>`, `<array>`, `<algorithm>`, `<vector>`, `<iostream>`.

8. **Variant visitation / mangling follow-ons after the pattern-struct boundary fix**: `<variant>` no longer stops on unexpanded `PackExpansionExprNode` nodes from parser-owned `$pattern__` structs, but it now exposes later template-instantiation gaps around `__get`, `__emplace`, `_S_apply_all_alts`, and an eventual Itanium mangling `unknown type` abort. Affects: `<variant>`.

9. **Ambiguous overload resolution**: `__to_unsigned_like` in ranges has multiple overloads that the overload resolver treats as ambiguous. Affects: `<ranges>`.

10. **Stack overflow during deep template instantiation**: Headers like `<barrier>`, and `<chrono>` trigger 6000-7500+ template instantiations that exhaust the stack. Affects: `<barrier>`, `<chrono>`, `<condition_variable>`.

11. **Base class member access in codegen**: Generated code fails to find members inherited from base classes (e.g., `_M_start` in `_Vector_impl`, `_M_impl` in `list`, `first` in `iterator`). Affects: `<vector>`, `<list>`, `<map>`.

12. **Late iostream-family codegen / IR lowering crash**: After the InstantiationContext fix below, `<iostream>` gets through parsing and much deeper into codegen before crashing in `IROperandHelpers::toIrValue` after `_S_empty`/`_S_size`/`move` failures. `<sstream>` / `<fstream>` still need targeted retests to see whether they now fail in the same later phase. Affects: `<iostream>`, likely `<sstream>`, `<fstream>`.

### Recent Fixes (2026-04-23)

1. **Nested member class template constructor/destructor parsing**: `parse_member_function_template` checked the constructor name against `struct_node.name()`, which for nested member class templates is the fully-qualified form (e.g. `Outer::_Storage`) or a `$pattern_...` partial-specialization name. The constructor in source is spelled with just the simple base name, so the check silently failed and the template-constructor was re-parsed as a return-typed function, causing `Expected identifier token` on the first parameter. The comparison now also accepts the simple name (after the last `::`) and the base name (with any `$pattern_...` suffix stripped). Separately, member class template bodies (both primary and partial specializations) did not handle `~StructName()` destructors and fell through to `parse_type_and_name()`, which rejected `~` with `Expected type specifier`. A small `skipMemberStructTemplateDestructor` helper now consumes the destructor declaration (including trailing noexcept/override/= default/= delete and the body) the same way `skipMemberStructTemplateConstructor` already consumed constructors; the destructor is re-parsed during instantiation alongside other members. Regression test: `tests/test_nested_member_template_ctor_dtor_ret0.cpp`.

   Linux/libstdc++ impact: `<optional>` previously stopped at `_Optional_payload_base::_Storage(in_place_t, _Args&&...)` on line 209 of `<optional>`, and then at the partial specialization's destructor on line 259. Both parser stops are gone; the header now parses completely and fails later in codegen on deeper deferred `_Optional_base` / `_Optional_payload` placeholder resolution.



1. **Namespaced implicit template instantiations can now keep lazy member-function stubs without replaying every deferred body immediately, and dependent placeholder member calls stop forcing concrete-class resolution**: implicit template classes with qualified names now take the same stub-registration path as other lazy instantiations, but deferred-body replay still stays enabled for existing non-namespaced tests so the main suite keeps its previous behavior. Separately, member-postfix resolution now recognizes incomplete template-instantiation placeholders and leaves those calls dependent instead of trying to materialize a concrete class too early. This is enough to make the focused std-style `pair::swap` unused-body regression compile again. Regression test: `tests/std/test_std_pair_swap_deleted_member.cpp`.

### Recent Fixes (2026-04-08)

1. **Binary-operator overload matching now resolves self-referential template parameter types by canonical name instead of raw pattern layout state**: member operators such as `Iter<T>::operator-(const Iter&)` and `Box<T>::operator==(const Box&)` no longer stop matching just because the uninstantiated pattern happens to have a known-sized non-dependent member (for example a pointer or `int`). The resolver now only rewrites genuine self-references, stripping nested-scope and template-hash suffixes before comparing names, which keeps the fix aligned with the existing self-reference handling used later in codegen. Regression tests: `tests/test_member_operator_template_pointer_self_ref_minus_ret0.cpp`, `tests/test_member_operator_template_mixed_self_ref_eq_ret0.cpp`.

### Recent Fixes (2026-04-07)

1. **Scoped-enum binary diagnostics now respect valid overloaded operators, and functional-style casts keep the resolved user-defined `TypeIndex` metadata**: sema no longer rejects expressions like `memory_order_expr | __memory_order_modifier(...)` before the parser-recorded overload can be used, and parenthesized casts such as `__memory_order_modifier(__m & mask)` now preserve their enum/alias type identity instead of dropping back to category-only metadata. This moves `<atomic>` and `<latch>` past the old `bits/atomic_base.h` scoped-enum failure into later codegen-only follow-ons. Regression test: `tests/test_scoped_enum_mixed_operator_overload_ret0.cpp`.

2. **Lazy member-resolution cache entries are now invalidated when a `TypeInfo` replaces its owned `StructTypeInfo`**: reparsing the same pattern struct no longer leaves cached `StructMember*` pointers dangling across `TypeInfo::setStructInfo()`. This removes the later `remove_volatile` / `__is_integral_helper` Itanium-mangling abort that still stopped `<atomic>` and `<latch>` after the earlier template-instantiation fix, and moves both headers to deeper follow-on failures. Regression test: `tests/test_lazy_member_cache_reparse_type_trait_ret42.cpp`.

3. **Namespace-scope post-`struct` variables now keep brace initializers as real initializer lists, and empty complete objects now have size 1 as required by the standard**: declarations such as `struct Cpo { ... } cpo = {}, cpo2{};` no longer get their `{}` rewritten to an `int` placeholder during parsing, and empty structs no longer report `total_size == 0` after layout finalization. This removes the bogus `_Synth3way` / `_Decay_copy` copy-initialization failures that had started blocking `<span>`, `<array>`, and `<string_view>` earlier in the ranges CPO setup. Regression tests: `tests/test_namespace_scope_struct_brace_init_ret0.cpp`, `tests/test_empty_struct_complete_object_size_ret0.cpp`.

### Recent Fixes (2026-04-06)

1. **Constructor-style parsing now treats struct/class typedefs and using-aliases as temporary object construction instead of plain function-call lookup**: aliases such as `PairAlias(1, 2)` and libstdc++'s `wstring(__s.data(), __s.data() + __s.size())` now resolve through the class constructor path, including aliases that refer to template instantiations and aliases found through enclosing namespaces. This moves `<string>`, `<ranges>`, and `<stdexcept>` past the old `No matching function for call to 'wstring'` blocker. Regression tests: `tests/test_type_alias_struct_paren_ctor_ret0.cpp`, `tests/test_type_alias_template_instantiation_paren_ctor_ret0.cpp`.

2. **Deferred template-base instantiation now substitutes/evaluates non-type expression arguments before instantiating the base**: recursive bases such as `_Tuple_impl<_Idx + 1, _Tail...>` no longer leave the raw expression argument unresolved during deferred-base replay, which moves `<tuple>` past its earlier post-parse pack-expansion boundary into a later `_Head_base` / recursion crash. Regression test: `tests/test_deferred_template_base_non_type_expr_ret0.cpp`.

3. **Declaration-vs-direct-initialization disambiguation now keeps qualified static/member calls as expression arguments even when they start with a known type name**: local declarations such as `Arg arg(Helper::make(7));` no longer get misread as function declarations just because `Helper` is a known type and the initializer begins with `Helper::...`. This fixes the `std::__str_concat` local `_Str __str(_Alloc_traits::_S_select_on_copy(__a));` parse path and moves `<string>`, `<ranges>`, and `<stdexcept>` beyond the old phase-1 `__str` error to later crashes. Regression test: `tests/test_qualified_static_call_direct_init_ret0.cpp`.

4. **Template-expression substitution now preserves alias-chain modifiers when a dependent member alias eventually substitutes to a template parameter**: when a templated body replays `sizeof(value_type)`, `sizeof(pointer_type)`, or `sizeof(array_type[10])`, the substitutor now first remaps the alias spelling back to the canonical template-parameter name, then reapplies the alias chain's own CV/pointer/array/function modifiers before layering on the outer use-site modifiers. This removes the old `bits/atomic_wait.h` `sizeof(__waiter_type)` static-assert failure, moves `<atomic>` past the former unknown-type mangling abort into later codegen fallout, and moves `<latch>` to a different later compile error around scoped-enum bitwise operators. Regression test: `tests/test_member_alias_sizeof_static_assert_ret0.cpp`.

### Recent Fixes (2026-04-05)

1. **Deferred explicit function-template instantiation now allows namespace-qualified calls to supply some template arguments explicitly and deduce the rest from substituted call arguments**: when a templated body replays a call such as `ns::f<Tag>(value)`, the explicit-instantiation path no longer rejects it just because not every template parameter was written in the source. `ExpressionSubstitutor` also stops trying to instantiate namespace owners like class templates during this recovery path. This moves `<atomic>` / `<latch>` past the old `std::__atomic_impl::__compare_exchange<_AtomicRef>` blocker. Regression test: `tests/test_namespace_qualified_explicit_function_template_deduction_ret1.cpp`.

2. **Compiler builtin registration now includes `__builtin_memcpy`, and direct-call lowering remaps it to libc `memcpy`**: libstdc++ atomic code can now use `__builtin_memcpy` in template bodies without tripping the earlier non-dependent-name error. This moves `<atomic>` / `<latch>` one phase deeper, to a later unknown-type mangling failure in `std::__atomic_impl::store`. Regression test: `tests/test_builtin_memcpy_ret0.cpp`.

3. **Instantiated template member-function codegen now preserves the owning struct context while emitting the function body**: unqualified references to static members inside class-template member functions no longer lose `current_struct_name_` during IR generation, so codegen can still resolve the struct's static constexpr globals through the normal member lookup path. Regression test: `tests/test_template_static_constexpr_member_call_ret0.cpp`.

4. **Function-declaration codegen now trusts `parent_struct_name()` as the owning-struct signal even when instantiated members do not set `is_member_function()`**: out-of-line class-template member functions keep the correct struct context for unqualified static member lookup during IR generation instead of being treated like free functions. Regression test: `tests/test_template_out_of_line_static_constexpr_member_ret0.cpp`.

### Recent Fixes (2026-04-04)

1. **Pointer-style GCC/Clang `__atomic*` builtins now synthesize exact signatures at call time**: when libstdc++ uses pointer-based atomics such as `__atomic_add_fetch(&_M_p, ...)`, FlashCpp now creates a concrete builtin declaration from the actual argument types and uses the pointee type as the return type instead of the old `unsigned long long` placeholder. This removes the immediate overload-resolution stop in `<atomic>` / `<latch>` and exposes the later qualified-template blocker instead. Regression test: `tests/test_atomic_builtin_pointer_intrinsics_ret0.cpp`.

2. **Post-parse boundary checking now skips parser-owned template-pattern structs, and chained member-access results keep concrete user-defined payload `TypeIndex` values**: the sema boundary no longer walks `$pattern__` class templates that intentionally still contain pack-expansion helpers, which moves `<variant>` past its earlier post-parse `PackExpansionExprNode` stop. Separately, member-load IR now preserves valid non-native payload type indices across chained accesses, which keeps simple inherited payload accesses working instead of collapsing them back to category-only placeholders. Regression tests: new `tests/test_member_chain_payload_base_ret0.cpp`, plus targeted std-header retest `tests/std/test_std_variant.cpp`.

3. **Statement parsing now treats leading identifiers followed by expression-only operators as expressions, even if an outer type alias shares the same name**: this prevents member/object statements such as `__r += __d.count();` from being misparsed as declarations just because a namespace-scope type alias named `__r` is visible elsewhere. This removes the old `bits/chrono.h` `duration::operator+=` / `operator-=` parse stop, letting `<chrono>` and `<shared_mutex>` progress to later semantic/codegen blockers instead. Regression test: `tests/test_member_shadows_outer_type_compound_assign_ret42.cpp`.


### Recent Fixes (2026-04-03)

1. **Compiler builtin registration now covers `__builtin_memcmp` plus the libstdc++-visible `__atomic*` names**: `__builtin_memcmp` is now declared/reported like the other compiler-known builtins and direct-call lowering remaps it to libc `memcmp`. The builtin registry and `__has_builtin` handling also now expose the GCC/Clang atomic builtin family used by `<atomic>` / `<latch>`, which removes the earlier phase-1 name-lookup stop and exposes the remaining typed-signature gap at `__atomic_add_fetch`. Regression tests: `tests/test_builtin_memcmp_template_ret0.cpp` and `tests/test_has_builtin_atomic_ret2.cpp`.

2. **Pointer/reference cast IR now preserves the canonical target `TypeIndex` and pointer depth**: `static_cast`, `const_cast`, and `reinterpret_cast` no longer collapse pointer targets to category-only placeholder metadata during codegen. This fixes CRTP-style `static_cast<const Derived*>(this)->member` lowering and removes the earlier `<optional>` `_M_is_engaged()` abort caused by `type_index=0`. Regression tests: new `tests/test_crtp_static_cast_this_member_ret0.cpp` plus existing cast coverage `tests/test_static_cast_base_ref_conv_op_ret0.cpp` and `tests/test_xvalue_all_casts_ret0.cpp`.

3. **Deferred template-base resolution now materializes placeholder base arguments before fallback**: when a deferred base argument still points at a template-instantiation placeholder with no `StructTypeInfo`, the class-template instantiator now tries to materialize the concrete base specialization instead of blindly carrying the placeholder forward. This moves `<optional>` past the earlier placeholder-loss point, though it still fails later on another deferred-base placeholder (`type_index=2736`) and therefore remains in the codegen-error bucket for now.

4. **Template-parameter substitution now materializes concrete class-template placeholders without regressing alias-member or template-template lookups**: when a substituted type argument is itself a concrete-but-lazy template-instantiation placeholder, qualified lookup now eagerly resolves it to the registered instantiated class name, but only for the cases that are safe to do before the dedicated `::type` and template-template parameter paths run. This fixes deferred codegen for direct CRTP-style base casts and defaulted placeholder bases while preserving earlier array-alias and template-template regressions. Regression tests: `tests/test_deferred_base_placeholder_codegen_ret0.cpp`, `tests/test_deferred_base_default_arg_placeholder_ret0.cpp`, plus existing `tests/test_template_type_alias_array_member_brace_init_ret0.cpp` and `tests/template_template_with_member_ret0.cpp`.

### Recent Fixes (2026-04-02)

1. **Builtin wide `wmemchr` declarations now match the standard const/non-const overload set**: the compiler-used builtin declarations now register both `wchar_t* wmemchr(wchar_t*, ...)` and `const wchar_t* wmemchr(const wchar_t*, ...)`, instead of a non-standard mixed signature. This removes the early `bits/char_traits.h` ambiguity that previously stopped `<string_view>` / `<iostream>` before later semantic/codegen phases. Regression coverage: existing `tests/std/test_wmemchr.cpp` plus targeted std-header repro `tests/std/test_std_wstring_view_find_ret0.cpp`.

2. **Binary operator parsing now instantiates matching free operator templates, and free-operator codegen reuses the instantiated mangled name**: infix expressions like `a == b` / `a - b` now trigger the same template-instantiation machinery that direct `operator==(a, b)` calls already used, and the later call emission now honors the instantiated function's stored mangled name. This fixes plain templated free operator overloads and removes one blocker from the standard-library iterator/string-view path. Regression tests: `tests/test_operator_template_eq_ret0.cpp`, `tests/test_operator_template_minus_ret0.cpp`.

### Recent Fixes (2026-04-01)

1. **Template alias array members now preserve array shape through instantiation**: array aliases such as `using type = T[N];` now keep their array metadata and original dimension expressions through template instantiation. This fixes the `std::array<int, N>` aggregate brace-init parser blocker: `typename __array_traits<_Tp, _Nm>::_Type _M_elems;` is now recognized as an array member during aggregate brace elision. Regression test: `tests/test_template_type_alias_array_member_brace_init_ret0.cpp`.

2. **Dependent template placeholders now carry type-owned InstantiationContext metadata in more creation paths**: several placeholder/template-specialization creation sites now attach an InstantiationContext immediately after `setTemplateInstantiationInfo(...)`. This removes the earlier `<iostream>` abort in `ConstExprEvaluator_Core.cpp:3314`; the header now progresses to later codegen/IR-lowering failures instead of dying during constexpr binding reconstruction.

### Recent Fixes (2026-03-31)

1. **Function pointer signatures now survive template instantiation and deferred/lazy member materialization**: Several template paths rebuilt `TemplateTypeArg` or `TypeSpecifierNode` values from bare `TypeIndex` metadata, silently discarding `function_signature`. This broke Itanium mangling for instantiated function-pointer parameters such as `__gnu_cxx::__stoa(_TRet (*__convf)(...), ...)` from `<string>`. The fix preserves `function_signature` in lazy member instantiation, free-function template instantiation, and the metadata used for outer-template bindings / stored template args. Regression tests: existing `tests/test_funcptr_template_signature_ret0.cpp` plus new `tests/test_funcptr_lazy_member_signature_ret0.cpp`.

2. **`<compare>` targeted retest now compiles again**: `tests/std/test_std_compare_ret42.cpp` currently compiles in ~20ms on Linux with the current branch state. The older `weak_ordering` constructor note was stale.

### TODO: Friend class access control — move to TypeIndex-based resolution

The current friend class implementation stores `StringHandle` names and matches them via string comparisons (with `$hash` / `_pattern` stripping). This is a semantic deviation from the C++ standard: friend declarations should resolve to specific *entities* (types), not string names. The current approach has two known approximations:

1. **Unqualified name collision**: A top-level `Foo` and `ns::Foo` could theoretically both match a friend entry stored as `"Foo"`, though this doesn't occur in practice with standard library headers.
2. **String manipulation fragility**: Access checks rely on stripping internal naming suffixes (`$hash`, `_pattern_P`, etc.) which couples the access checker to the template instantiation naming scheme.

**The fully correct approach** (per [class.friend]/2 and [temp.friend]):

1. **Add `TypeIndex type_index_` to `StructTypeInfo`** — set during `TypeInfo::setStructInfo()`. Currently getting from `StructTypeInfo*` → `TypeIndex` requires a linear scan of `gTypeInfo` or a `gTypesByName` lookup.

2. **Store `TypeIndex` in friend entries instead of `StringHandle`** — at `addFriendClass` time, try `gTypesByName` lookup (both unqualified and namespace-qualified). If found, store the `TypeIndex`. If not found (friend class not yet declared — common for forward-referenced friends), store the `StringHandle` + namespace context for deferred resolution.

3. **Add a deferred resolution pass** — after all types in the translation unit are parsed, resolve remaining string-based friend entries to `TypeIndex` values via `gTypesByName`.

4. **Simplify `checkFriendClassAccess`** — compare `TypeIndex` values (O(1) integer comparison). For template instantiations, also check the primary template's `TypeIndex`. No string manipulation needed.

**Incremental path**: Try eager `TypeIndex` resolution at registration time, fall back to `StringHandle` for forward-reference cases. Check `TypeIndex` first in the access checker, then fall through to string matching for unresolved entries. This gets correctness for most cases without requiring the deferred resolution pass upfront.
