# Standard Header Tests

This directory contains test files for C++ standard library headers to assess FlashCpp's compatibility with the C++ standard library.

## How to build and run

### 1. Build the compiler (required first)

From the repo root in PowerShell. Do **not** run the test runner in parallel with this:

```powershell
.\build_flashcpp.bat
```

Produces `x64\Sharded\FlashCppMSVC.exe`. On Windows the compiler auto-discovers MSVC STL and Windows SDK (UCRT) include paths; you normally do not need `-I` flags.

### 2. Run one header test (recommended on Windows)

`tests/std` is **not** part of the default full suite. Pass an explicit file name so the runner searches under `tests/` recursively (including `tests/std/`):

```powershell
pwsh tests/run_all_tests.ps1 test_std_limits.cpp
pwsh tests/run_all_tests.ps1 test_std_iterator.cpp
```

That compiles with FlashCpp, links with MSVC `link.exe`, and runs the resulting binary (expects `main` return `0` unless the name encodes another expected code via `_retN`).

Compile-only check without the runner:

```powershell
.\x64\Sharded\FlashCppMSVC.exe tests\std\test_std_limits.cpp
```

### 3. Batch / platform scripts

| Platform | Command | Notes |
|----------|---------|--------|
| Windows | `pwsh tests/std/test_standard_headers.ps1` | Builds if needed; probes many `#include <header>` cases with auto-detected `-I` paths |
| Linux | `cd tests/std && ./test_std_headers_comprehensive.sh [timeout_secs]` | Default timeout 10s; uses `make release` / libstdc++ includes |

### 4. Adding or updating a header probe

1. Add `tests/std/test_std_<header>.cpp` (typically `#include <header>` + `main` returning `0`).
2. Rebuild FlashCpp, then run via `pwsh tests/run_all_tests.ps1 test_std_<header>.cpp`.
3. Update the status table below with the first real stop and the language mechanism to fix.
4. For a language bug found via STL, also add a reduced non-`std` regression under `tests/` (e.g. `test_*_ret0.cpp`) before specializing on library names.

Language regressions that already pass belong in `tests/` (main suite), not only here.

## Current Status

> **Notes** column = current first stop. Blockers section below lists the mechanism to fix.

| Header | Test File | Status | Notes |
|--------|-----------|--------|-------|
| `<limits>` | `test_std_limits.cpp` | ✅ Compiled | ~5362ms (`TOTAL`) / ~5.4s wall (retested 2026-07-26, Windows/MSVC STL 14.44). |
| `<type_traits>` | `test_std_type_traits.cpp` | ✅ Compiled | ~1170ms (`TOTAL`) / ~1.3s wall (retested 2026-07-26, Windows/MSVC STL 14.44). |
| `<compare>` | `test_std_compare_ret42.cpp` | ✅ Compiled | ~0.06s (retested 2026-05-23, Linux/libstdc++-14). |
| `<version>` | `test_std_version.cpp` | ✅ Compiled | ~41ms |
| `<source_location>` | `test_std_source_location.cpp` | ✅ Compiled | ~41ms |
| `<numbers>` | N/A | ✅ Compiled | ~510ms |
| `<initializer_list>` | N/A | ✅ Compiled | ~32ms. Direct `std::initializer_list<T> values = {...}` object list-initialization is now covered by `tests/test_std_initializer_list_direct_brace_ret0.cpp` (retested 2026-04-20). |
| `<ratio>` | `test_std_ratio.cpp` | ❌ Semantic Error | ~4169ms (`TOTAL`) / ~4.2s wall (retested 2026-07-26, Windows/MSVC STL 14.44). `ratio_equal<...>::value` is not materialized as a constant expression. |
| `<optional>` | `test_std_optional.cpp` | ❌ Parse Error | ~8926ms (`TOTAL`) / ~9.0s wall (retested 2026-07-26, Windows/MSVC STL 14.44). First hard stop is structural class-type non-type template parameters in MSVC `optional:269`. |
| `<any>` | `test_std_any.cpp` | ❌ Semantic Error | ~9.5s wall; full parse completes in ~7.9s (retested 2026-07-26, Windows/MSVC STL 14.44). Interleaved `__declspec(allocator)` and bare `__cdecl` function-type aliases now parse; current stop is ambiguous `_Get_rest` member lookup. |
| `<utility>` | `test_std_utility.cpp` | ✅ Compiled | ~1697ms (`TOTAL`) / ~1.7s wall (retested 2026-07-26, Windows/MSVC STL 14.44). Template-parameter spelling now wins over stale dependent `TypeInfo`, so `pair<int, float>::first` has type `int`. Object generation succeeds; the linked probe still crashes at runtime, leaving a separate codegen issue. |
| `<concepts>` | `test_std_concepts.cpp` | ✅ Compiled | ~1350ms (`TOTAL`) / ~1.4s wall (retested 2026-07-26, Windows/MSVC STL 14.44). |
| `<bit>` | `test_std_bit.cpp` | ✅ Compiled | ~1083ms (retested 2026-05-23, Linux/libstdc++-14). |
| `<string_view>` | `test_std_string_view.cpp` | ❌ Lazy Replay Error | ~11.5s wall; full parse completes in ~9.8s (retested 2026-07-26, Windows/MSVC STL 14.44). Current stop is lazy member-body replay for `_String_view_iterator::operator+`. |
| `<string>` | `test_std_string.cpp` | ❌ Compile Error | ~6.19s (`TOTAL`) / ~6.68s wall (retested 2026-05-27, Linux/libstdc++-14). Completed class-template cache hits no longer consume template-depth budget; current first hard error is now depth-guarded recursive `basic_string` instantiation. |
| `<array>` | `test_std_array.cpp` | ✅ Compiled | ~2.64s (retested 2026-05-23, Linux/libstdc++-14). |
| `<algorithm>` | `test_std_algorithm.cpp` | 💥 Crash | ~4.95s (retested 2026-05-21, Linux/libstdc++-14). The shared `ptr_traits` member-alias-template target now parses; current run reaches late IR/codegen (`std::partial_ordering` missing resolved constructor / unresolved semantic type category 25) and can still crash after deep template replay. |
| `<span>` | `test_std_span.cpp` | ❌ Template Default Error | ~8646ms (`TOTAL`) / ~8.7s wall (retested 2026-07-26, Windows/MSVC STL 14.44). Could not evaluate non-type template default parameter 1 of `std::span`. |
| `<tuple>` | `test_std_tuple.cpp` | ❌ Template Replay Error | ~2032ms (`TOTAL`) / ~2.1s wall (retested 2026-07-26, Windows/MSVC STL 14.44). Partial-specialization nested out-of-line `tuple` cannot be attached through source-member identity mapping. |
| `<vector>` | `test_std_vector.cpp` | ❌ Semantic Error | ~9708ms (`TOTAL`) / ~9.8s wall (retested 2026-07-26, Windows/MSVC STL 14.44). Interleaved `__declspec(allocator)` now parses; current stop is `auto` deduction for allocator state member `_Mylast`. |
| `<deque>` | `test_std_deque.cpp` | 💥 Crash | ~2464ms (retested 2026-04-11). |
| `<list>` | `test_std_list.cpp` | ❌ Compile Error | ~2940ms (retested 2026-05-12, Linux/libstdc++-14). The shared `_Head_base` default-NTTP stop remains fixed; after raising template nesting limits the first hard error is still depth-guarded, now `Max template instantiation depth (40) exceeded for 'polymorphic_allocator'`. |
| `<queue>` | `test_std_queue.cpp` | 💥 Crash | ~2522ms (retested 2026-04-11). |
| `<stack>` | `test_std_stack.cpp` | 💥 Crash | ~2464ms (retested 2026-04-11). |
| `<memory>` | `test_std_memory.cpp` | ❌ Compile Error | ~6.64s (`TOTAL`) / ~7.15s wall (retested 2026-05-27, Linux/libstdc++-14). `__make_move_if_noexcept_iterator` still emits non-fatal overload noise, but the current first hard error remains depth-guarded recursive `basic_string` instantiation. |
| `<functional>` | `test_std_functional.cpp` | ❌ Compile Error | ~4.76s (`TOTAL`) / ~5.13s wall (retested 2026-05-27, Linux/libstdc++-14). `__make_move_if_noexcept_iterator` still emits non-fatal overload noise, but the current first hard error remains depth-guarded `rebind`. |
| `<map>` | `test_std_map.cpp` | ❌ Compile Error | ~2498ms (retested 2026-04-30, Linux/libstdc++-14). No longer stops at `Missing TypeInfo while computing template argument size`; it now reaches `Unregistered dependent placeholder type reached template argument classification`. |
| `<set>` | `test_std_set.cpp` | ❌ Compile Error | ~2350ms (retested 2026-04-12). The earlier variable-template/type-traits arity blocker is gone. Current first error is later in the Windows UCRT headers: "No matching function for call to '__stdio_common_vfwprintf'". |
| `<ranges>` | `test_std_ranges.cpp` | ❌ Compile Error | ~1534s compile-only / >25 min (retested 2026-07-23, Windows/MSVC STL 14.44). Parse/noexcept stops fixed; full compile now exhausts template depth on variadic `invoke` / `operator()` recursion. |
| `<iostream>` | `test_std_iostream.cpp` | 💥 Crash | ~4559ms (retested 2026-04-11). |
| `<sstream>` | `test_std_sstream.cpp` | 💥 Crash | ~4565ms (retested 2026-04-11). |
| `<fstream>` | `test_std_fstream.cpp` | 💥 Crash | ~4642ms (retested 2026-04-11). |
| `<chrono>` | `test_std_chrono.cpp` | ❌ Compile Error | ~6638ms (retested 2026-04-11). Call to deleted function 'swap'. |
| `<atomic>` | `test_std_atomic.cpp` | ✅ Compiled | ~838ms (retested 2026-04-24, Linux/libstdc++). **NEW: Now compiles successfully on Linux!** Previous deferred member function codegen errors are resolved. |
| `<new>` | `test_std_new.cpp` | ✅ Compiled | ~56ms |
| `<exception>` | `test_std_exception.cpp` | ✅ Compiled | ~368ms (retested 2026-04-24, Linux/libstdc++). **NEW: Now compiles successfully on Linux!** The `exception_ptr` copy-vs-move-constructor ambiguity is resolved by the rvalue overload-rank fix. Regression: `tests/test_rvalue_ref_overload_preference_ret0.cpp`. |
| `<stdexcept>` | `test_std_stdexcept.cpp` | ❌ Compile Error | ~5.95s (`TOTAL`) / ~6.42s wall (retested 2026-05-25, Linux/libstdc++-14). First hard error now matches `<memory>/<string>`: `Could not evaluate non-type template default for parameter 1 of '__hash_enum'`. |
| `<typeinfo>` | `test_std_typeinfo_ret0.cpp` | ✅ Compiled | ~46ms (retested 2026-04-30, Linux/libstdc++-14). Sema now models pointer arithmetic (`T* + integral`, `T* - integral`, `T* - T*`) so the ternary in `type_info::name()` (`__name[0] == '*' ? __name + 1 : __name`) gets a sema-owned exact result type and codegen no longer throws. Regression: `tests/test_ternary_pointer_arithmetic_branches_ret0.cpp`. |
| `<typeindex>` | N/A | ❌ Codegen Error | ~640ms (retested 2026-04-11). "Cannot use copy initialization with explicit constructor". |
| `<numeric>` | `test_std_numeric.cpp` | ✅ Compiled | ~7529ms (retested 2026-05-25, Linux/libstdc++-14). **NOW WORKS**: ternary common-type fix resolved `numeric_limits` member constexpr folding. Builtin `__builtin_huge_val`/`__builtin_nan` families now handled in constexpr evaluator. |
| `<iterator>` | `test_std_iterator.cpp` | ❌ Codegen Error | ~9.1s wall; full parse completes in ~7.3s (retested 2026-07-26, Windows/MSVC STL 14.44). First hard stops remain sema-normalized return lowering for `begin`/`end`, `view_interface` equality, and incomplete aggregate layout. |
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
| `<latch>` | `test_std_latch.cpp` | ❌ Codegen Error | ~1.90s wall (retested 2026-05-28, Linux/libstdc++-14). Variadic fixed-parameter conversion annotation still applies (the prior `int -> long` direct-call Phase 15 miss in `__platform_wait`/`__platform_notify` remains gone); current first stops are unchanged deferred constructor materialization / receiver-normalization gaps around `_Spin`, `std::__mutex_base`, and `_EntersWait::value`. |
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
| `<cmath>` | `test_std_cmath.cpp` | ❌ Compile Error | ~16.55s (retested 2026-05-21, Linux/libstdc++-14). The shared `ptr_traits` member-alias-template target now parses and special-function replay goes much deeper; current first fatal stop is `std::__numeric_limits_base::has_denorm` unresolved constexpr initialization. |
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

## Current blockers (Windows/MSVC)

First stop and the language mechanism to fix. Not a session work-log.

| Header | Stop | Mechanism to fix |
|--------|------|------------------|
| `<optional>` | Parse: structural class-type NTTP | Structural non-type template arguments and instantiation identity |
| `<ratio>` | Sema/constexpr: missing `ratio_equal<...>::value` | Static member materialization through trait instantiation |
| `<any>` | Sema: ambiguous `_Get_rest` member function | Dependent member overload lookup after allocator/function-type parsing |
| `<span>` | Template default: extent parameter | Dependent non-type template default evaluation |
| `<tuple>` | Replay: partial-spec nested out-of-line member | Stable source-member identity across partial specializations |
| `<vector>` | Sema: cannot deduce `_Mylast` `auto` | Member-access result typing in allocator state |
| `<string_view>` | Lazy replay: `_String_view_iterator::operator+` | Lazy member-body replay and source binding |
| `<iterator>` | Codegen: sema return/ternary/`begin`/`end` lowering; residual `view_interface` aggregate holes | Exact result types for ternary/return; complete types for CPO/`auto` results in ranges interface members |
| `<ranges>` | Template-depth / `invoke` recursion | Variadic `invoke` / CPO instantiation |

Regressions for the 2026-07-26 Windows fixes: `tests/test_template_member_type_prefers_parameter_spelling_ret0.cpp`, `tests/test_interleaved_declspec_allocator_ret42.cpp`, `tests/test_interleaved_declspec_cv_qualifiers_ret42.cpp`, `tests/test_interleaved_declspec_const_assignment_fail.cpp`, and `tests/test_using_bare_function_type_calling_convention_ret42.cpp`. The shared declaration-specifier parser now carries `const`/`volatile` across interleaved Microsoft `__declspec` specifiers, and semantic analysis rejects assignment to the resulting const-qualified object. Regressions for class-template layout/friend fixes: `tests/test_class_tmpl_cross_spec_complete_layout_ret0.cpp`, `tests/test_class_tmpl_friend_plus_byvalue_ret0.cpp`. Regressions for recent `<limits>` fixes: `tests/test_unused_inline_no_emit_ret0.cpp`, `tests/test_used_inline_still_emitted_ret0.cpp`, `tests/test_selectany_comdat_ret0.cpp`, plus earlier typedef-assign / rvalue-assign tests.
