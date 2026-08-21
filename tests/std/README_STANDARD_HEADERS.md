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
For a full parallel run, unexpected compile/link/runtime failures are retried once
serially before the summary. Recovered high-load flakes remain listed in the
output, while deterministic failures still fail the run.

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

> **Notes** column = current first stop. **Current blockers** below lists the language mechanism to fix. Prefer reduced non-`std` regressions under `tests/` when fixing a stop.

| Header | Test File | Status | Notes |
|--------|-----------|--------|-------|
| `<limits>` | `test_std_limits.cpp` | ✅ Runs | 5.06s compiler total (retested 2026-08-19, Windows/MSVC STL 14.44). |
| `<type_traits>` | `test_std_type_traits.cpp` | ✅ Runs | 1.27s compiler total (retested 2026-08-19, Windows/MSVC STL 14.44). |
| `<compare>` | `test_std_compare_ret42.cpp` | ✅ Runs | Manual `std::strong_ordering` stand-in; not a real `<compare>` include (0.07s compiler total, retested 2026-08-19). |
| `<version>` | `test_std_version.cpp` | ✅ Runs | 0.09s compiler total (retested 2026-08-19, Windows/MSVC STL 14.44). |
| `<source_location>` | `test_std_source_location.cpp` | ✅ Runs | 0.11s compiler total (retested 2026-08-19, Windows/MSVC STL 14.44). |
| `<numbers>` | N/A | ✅ Compiled | ~510ms |
| `<initializer_list>` | N/A | ✅ Compiled | ~32ms. Direct `std::initializer_list<T> values = {...}` object list-initialization is now covered by `tests/test_std_initializer_list_direct_brace_ret0.cpp` (retested 2026-04-20). |
| `<ratio>` | `test_std_ratio.cpp` | ✅ Runs | 5.83s compiler total (retested 2026-08-19, Windows/MSVC STL 14.44). |
| `<optional>` | `test_std_optional.cpp` | ❌ Codegen Error | Frontend reaches IR (retested 2026-08-19, Windows/MSVC STL 14.44). Instantiated `std::begin`/`std::end` wrappers recover member return types; `_Has_value` is still missing from the `optional` layout. |
| `<any>` | `test_std_any.cpp` | ❌ Codegen Error | 14.53s compile (retested 2026-08-16, Windows/MSVC STL 14.44). The first remaining stop is failed `_Seek_to` template overload materialization, followed by `view_interface` reaching a missing `operator==`. |
| `<utility>` | `test_std_utility.cpp` | ✅ Runs | 1.59s compiler total (retested 2026-08-19, Windows/MSVC STL 14.44). |
| `<concepts>` | `test_std_concepts.cpp` | ✅ Runs | 1.28s compiler total (retested 2026-08-19, Windows/MSVC STL 14.44). |
| `<bit>` | `test_std_bit.cpp` | ✅ Runs | 1.74s compiler total (retested 2026-08-19, Windows/MSVC STL 14.44). |
| `<string_view>` | `test_std_string_view.cpp` | ❌ Compile Error | Lazy `_String_view_iterator::operator+` replay still fails (retested 2026-08-19, Windows/MSVC STL 14.44). |
| `<string>` | `test_std_string.cpp` | ❌ Compile Error | 15.67s compile (retested 2026-08-19, Windows/MSVC STL 14.44). Sticky template-instantiation iteration limit while instantiating `allocator_traits`. |
| `<array>` | `test_std_array.cpp` | ❌ Link Error | 9.15s frontend compile (retested 2026-08-19, Windows/MSVC STL 14.44). Parsing, sema, and IR complete; link exposes unresolved CRTP/view calls, array-iterator members, and `std::move`. |
| `<algorithm>` | `test_std_algorithm.cpp` | ❌ Compile Error | 9.49s compile (retested 2026-08-19, Windows/MSVC STL 14.44). `_Stack_space` array bound still cannot find `_Optimistic_count` in constant expression. |
| `<span>` | `test_std_span.cpp` | ❌ Link Error | 9.10s frontend compile (retested 2026-08-19, Windows/MSVC STL 14.44). Parsing, sema, and IR complete; link exposes unresolved CRTP/view calls, reverse-iterator construction, range verification, and `std::move`. |
| `<tuple>` | `test_std_tuple.cpp` | ✅ Runs | 2.78s compiler total (retested 2026-08-19, Windows/MSVC STL 14.44). Constructor-template partial ordering, implicit base-constructor overload resolution, zero-argument default-template deduction, and static-member definition ownership are covered by reduced non-`std` regressions. |
| `<vector>` | `test_std_vector.cpp` | ❌ Compile Error | 9.78s compile (retested 2026-08-19, Windows/MSVC STL 14.44). `vector:1537:15`: Failed to instantiate template function at `_Pocca(_Al, _Right_al)`. |
| `<deque>` | `test_std_deque.cpp` | ❌ Codegen Error | 14.22s compile (retested 2026-08-16, Windows/MSVC STL 14.44). The first remaining stop is failed `_Seek_to` template overload materialization, followed by `view_interface` reaching a missing `operator==`. |
| `<list>` | `test_std_list.cpp` | ❌ Compile Error | 10.39s compile (retested 2026-08-19, Windows/MSVC STL 14.44). Past `_Pocma`/`noexcept`; now blocked on the sticky template-instantiation iteration limit while instantiating `allocator_traits`. |
| `<queue>` | `test_std_queue.cpp` | ❌ Compile Error | 10.01s compile (retested 2026-08-19, Windows/MSVC STL 14.44). Past included `<deque>` omitted-`typename`; now blocked on included `<vector>` `_Pocca`. |
| `<stack>` | `test_std_stack.cpp` | ❌ Codegen Error | 11.46s compile (retested 2026-08-16, Windows/MSVC STL 14.44). Included `<deque>` now clears `new`-expression pack expansion and reaches the same missing sema initializer conversion for `_Big_allocation_threshold`. |
| `<memory>` | `test_std_memory.cpp` | ❌ Compile Error | 9.49s compile (retested 2026-08-13 evening, Windows/MSVC STL 14.44). Included `<atomic>` now gets past `__iso_volatile_store32` and stops at `atomic:537:42`: No matching function for `_InterlockedCompareExchange128`. |
| `<functional>` | `test_std_functional.cpp` | ❌ Compile Error | 11.72s compile (retested 2026-08-15, Windows/MSVC STL 14.44). Included `<list>` still stops at `list:951:23` while instantiating `_Pocma(_Al, _Right_al)`. |
| `<map>` | `test_std_map.cpp` | ❌ Compile Error | 10.03s compile (retested 2026-08-19, Windows/MSVC STL 14.44). Instantiated `noexcept` still fails: `conjunction$…::value` is an undefined qualified identifier after `_Conjunction<static_cast<bool>(Trait::value), …>` base materialization. Member-template defaults that spell the injected-class-name (`Myself = pair`) now bind the current specialization (`tests/test_injected_class_name_member_default_ret0.cpp`); remaining `_Tree` traits still skip the `_Conjunction` NTTP. |
| `<set>` | `test_std_set.cpp` | ❌ Compile Error | 10.46s compile (retested 2026-08-19, Windows/MSVC STL 14.44). Same instantiated-`noexcept` `conjunction::value` stop as `<map>`. |
| `<ranges>` | `test_std_ranges.cpp` | ❌ Compile Error | 10.53s compile (retested 2026-08-19, Windows/MSVC STL 14.44). Sticky template-instantiation iteration limit remains. |
| `<iostream>` | `test_std_iostream.cpp` | ❌ Compile Error | 12.93s compile (retested 2026-08-19, Windows/MSVC STL 14.44). Instantiated `noexcept` cannot find `conjunction$…::value` (same deferred `_Conjunction` NTTP stop as `<map>`). |
| `<sstream>` | `test_std_sstream.cpp` | ❌ Compile Error | 11.99s compile (retested 2026-08-13 evening, Windows/MSVC STL 14.44). Same instantiated-`noexcept` stop as `<iostream>`. |
| `<fstream>` | `test_std_fstream.cpp` | ❌ Compile Error | 16.11s compile (retested 2026-08-19, Windows/MSVC STL 14.44). Same `allocator_traits` instantiation-limit stop as `<string>`. |
| `<chrono>` | `test_std_chrono.cpp` | ❌ Compile Error | 18.32s compile (retested 2026-08-19, Windows/MSVC STL 14.44). Included `<xstring>` now reaches the same `allocator_traits` instantiation-limit stop as `<string>`. |
| `<atomic>` | `test_std_atomic.cpp` | ❌ Compile Error | 1.82s compile (retested 2026-08-19, Windows/MSVC STL 14.44). Past `__iso_volatile_store32`; `atomic:537:42`: No matching function for `_InterlockedCompareExchange128`. |
| `<new>` | `test_std_new.cpp` | ❌ Link Error | 1.79s frontend compile (retested 2026-08-19, Windows/MSVC STL 14.44). Frontend compile now succeeds; link still misses `__ExceptionPtrCompare` / `terminate` / exception-ptr runtime symbols. |
| `<exception>` | `test_std_exception.cpp` | ❌ Link Error | 1.80s frontend compile (retested 2026-08-19, Windows/MSVC STL 14.44). Frontend compile now succeeds; same missing exception-runtime symbols as `<new>`. |
| `<stdexcept>` | `test_std_stdexcept.cpp` | ❌ Compile Error | 15.22s compile (retested 2026-08-18, Windows/MSVC STL 14.44). Included `<xstring>` now reaches the same `allocator_traits` instantiation-limit stop as `<string>`. |
| `<typeinfo>` | `test_std_typeinfo_ret0.cpp` | ❌ Link Error | 4.92s wall (retested 2026-08-13 evening, Windows/MSVC STL 14.44). Frontend compile now succeeds; link still misses RTTI/exception-runtime symbols (`type_info` dtor, `__type_info_root_node`, exception-ptr helpers). |
| `<typeindex>` | `test_std_typeindex.cpp` | ✅ Compiled (compile-only) | 2.17s compiler total (retested 2026-08-19, Windows/MSVC STL 14.44). No `main`; direct compile-only probe exits 0. |
| `<numeric>` | `test_std_numeric.cpp` | ❌ Link Error | 9.29s frontend compile (retested 2026-08-19, Windows/MSVC STL 14.44). Parsing, sema, and IR complete; link exposes unresolved CRTP/view calls and `std::move`. |
| `<iterator>` | `test_std_iterator.cpp` | ❌ Link Error | 9.54s frontend compile (retested 2026-08-19, Windows/MSVC STL 14.44). Parsing, sema, and IR complete; link exposes unresolved CRTP/view calls and `std::move`. |
| `<variant>` | `test_std_variant.cpp` | 💥 Crash | 8.75s compile (retested 2026-08-13 evening, Windows/MSVC STL 14.44). Stack overflow in alias-template materialization (`Parser::materializeAliasTemplateInstantiation` recursion). |
| `<csetjmp>` | N/A | ✅ Compiled | ~35ms |
| `<csignal>` | N/A | ✅ Compiled | ~140ms |
| `<stdfloat>` | N/A | ✅ Compiled | ~16ms (C++23) |
| `<spanstream>` | N/A | ✅ Compiled | ~44ms (C++23) |
| `<print>` | N/A | ✅ Compiled | ~52ms (C++23) |
| `<expected>` | N/A | ✅ Compiled | ~62ms (C++23) |
| `<text_encoding>` | N/A | ✅ Compiled | ~45ms (C++26) |
| `<stacktrace>` | N/A | ✅ Compiled | ~47ms (C++23) |
| `<barrier>` | N/A | ❌ Compile Error | 2.77s (retested 2026-08-13, Windows/MSVC STL 14.44). `swap` overload deduction fails during header inclusion; no crash in this probe. |
| `<coroutine>` | N/A | ✅ Compiled | 0.12s (retested 2026-08-13, Windows/MSVC STL 14.44). Direct MSVC include probe exits 0. |
| `<latch>` | `test_std_latch.cpp` | ❌ Compile Error | 1.77s compile (retested 2026-08-13 evening, Windows/MSVC STL 14.44). Same `_InterlockedCompareExchange128` stop as `<atomic>`. |
| `<shared_mutex>` | `test_std_shared_mutex.cpp` | ❌ Compile Error | 10.17s compile (retested 2026-08-13 evening, Windows/MSVC STL 14.44). Same `_InterlockedCompareExchange128` stop as `<atomic>`. |
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
| `<cmath>` | `test_std_cmath.cpp` | ✅ Runs | 3.30s compiler total (retested 2026-08-19, Windows/MSVC STL 14.44). Native `__builtin_bit_cast`, dependent block-scope alias ownership, static-member typing, and LLP64 `long double` sizing now carry `_Bit_cast` specializations through codegen. |
| `<system_error>` | N/A | ❌ Compile Error | 13.34s (retested 2026-08-13, Windows/MSVC STL 14.44). `swap` overload deduction fails during header inclusion; no crash in this probe. |
| `<scoped_allocator>` | N/A | ❌ Compile Error | 10.00s (retested 2026-08-13, Windows/MSVC STL 14.44). `swap` overload deduction fails during header inclusion. |
| `<charconv>` | N/A | ✅ Compiled | ~930ms |
| `<numbers>` | N/A | ✅ Compiled | ~510ms |
| `<mdspan>` | N/A | ✅ Compiled | 0.16s (retested 2026-08-13, Windows/MSVC STL 14.44). Direct MSVC include probe exits 0. |
| `<flat_map>` | N/A | ❌ Include Error | 0.03s (retested 2026-08-13, Windows/MSVC STL 14.44). Header is not present in the installed MSVC STL. |
| `<flat_set>` | N/A | ❌ Include Error | 0.03s (retested 2026-08-13, Windows/MSVC STL 14.44). Header is not present in the installed MSVC STL. |
| `<unordered_set>` | N/A | ❌ Compile Error | 2.77s (retested 2026-08-13, Windows/MSVC STL 14.44). `swap` overload deduction fails during header inclusion. |
| `<unordered_map>` | N/A | ❌ Compile Error | 2.74s (retested 2026-08-13, Windows/MSVC STL 14.44). `swap` overload deduction fails during header inclusion. |
| `<mutex>` | N/A | ❌ Compile Error | 14.27s (retested 2026-08-13, Windows/MSVC STL 14.44). `swap` overload deduction fails during header inclusion. |
| `<condition_variable>` | N/A | ❌ Compile Error | 10.62s (retested 2026-08-13, Windows/MSVC STL 14.44). `swap` overload deduction fails during header inclusion. |
| `<thread>` | N/A | ❌ Compile Error | 10.33s (retested 2026-08-13, Windows/MSVC STL 14.44). `swap` overload deduction fails during header inclusion. |
| `<semaphore>` | N/A | ❌ Compile Error | 7.57s (retested 2026-08-13, Windows/MSVC STL 14.44). `swap` overload deduction fails during header inclusion. |
| `<stop_token>` | N/A | ❌ Compile Error | 2.73s (retested 2026-08-13, Windows/MSVC STL 14.44). `swap` overload deduction fails during header inclusion. |
| `<bitset>` | N/A | ❌ Compile Error | 13.26s (retested 2026-08-13, Windows/MSVC STL 14.44). `swap` overload deduction fails during header inclusion. |
| `<execution>` | N/A | ❌ Compile Error | 9.87s (retested 2026-08-13, Windows/MSVC STL 14.44). `swap` overload deduction fails during header inclusion. |
| `<generator>` | N/A | ✅ Compiled | 0.12s (retested 2026-08-13, Windows/MSVC STL 14.44; C++23 header). Direct MSVC include probe exits 0. |

**Legend:** ✅ Runs / Compiled | ❌ Compile/Link/Parse/Include Error | 💥 Crash

## Current blockers (Windows/MSVC)

First stop and the language mechanism to fix. Not a session work-log.

| Header | Stop | Mechanism to fix |
|--------|------|------------------|
| `<optional>` | Codegen: `_Has_value` is missing from the `optional` layout | Complete inherited-member/layout materialization for `optional` |
| `<any>` / `<deque>` | Template instantiation: all `_Seek_to` overloads fail; codegen then reaches `view_interface` without `operator==` | Complete generic constrained iterator/member lookup and dependent operator materialization before IR |
| `<array>` / `<span>` / `<numeric>` / `<iterator>` | Link: unresolved generic CRTP/view and iterator calls | Emit/reach the resolved generic member definitions for CRTP/view and iterator operations |
| `<string>` / `<fstream>` / `<chrono>` / `<stdexcept>` / `<list>` | Template-instantiation iteration limit while instantiating `allocator_traits` | Variadic allocator-trait / construct replay without SoftProbe retry storms |
| `<map>` / `<set>` | Sema: instantiated `noexcept` cannot find `conjunction$…::value` | Complete deferred-base materialization of `_Conjunction<static_cast<bool>(Trait::value), …>::type` so `conjunction_v` has `::value` |
| `<vector>` / `<queue>` | Sema: `_Pocca(_Al, _Right_al)` overload/template instantiation fails | Preserve and resolve definition-bound dependent overload sets through allocator-trait member replay |
| `<string_view>` | Sema: failed lazy `_String_view_iterator::operator+` replay | Complete lazy member-operator body rematerialization for iterator wrappers |
| `<ranges>` | Template-instantiation iteration limit (sticky abort) | Variadic `invoke` / CPO instantiation without SoftProbe retry storms |
| `<stack>` | IR boundary: namespace-scope `_Big_allocation_threshold` lacks its sema-owned `int` to `size_t` initializer conversion | Ensure namespace/global variables materialized during header/template processing receive the same initialization-conversion annotation as local declarations |
| `<atomic>` / `<memory>` / `<latch>` / `<shared_mutex>` | Sema: no matching `_InterlockedCompareExchange128` (`long long*`, two `long long` values, `long long[2]`) | Model the MSVC 128-bit CAS intrinsic, including array-to-pointer decay of the comparand result |
| `<iostream>` / `<sstream>` | Sema: instantiated `noexcept` cannot find `conjunction$…::value` | Same deferred-`_Conjunction` NTTP evaluation as `<map>` / `<set>` |
| `<variant>` | Crash: stack overflow in `materializeAliasTemplateInstantiation` | Bound alias-template materialization recursion / detect cyclic alias instantiation |

## Follow-ons (not yet first-stop blockers)

- Nested class templates such as `StringVal<Types>` can still be cached while `Types` is a DependentArgs placeholder, freezing a false-branch `sizeof` bound before outer rematerialization finishes (`tests/test_conditional_t_var_bool_class_alias_sizeof_ret0.cpp` covers the cleared surface case).
- Empty trailing class-template packs are now recorded in the instantiation context (`tests/test_class_template_trailing_empty_pack_ret0.cpp`). Variadic `__is_nothrow_constructible(T, Args...)` in `noexcept` is covered by `tests/test_nothrow_constructible_pack_noexcept_ret0.cpp`; MSVC-style `conjunction` with `static_cast<bool>(Trait::value)` is covered by `tests/test_conjunction_static_cast_bool_nttp_noexcept_ret0.cpp` and `tests/test_conjunction_nothrow_assignable_noexcept_ret0.cpp`. Injected-class-name member-template defaults such as `template<class Myself = Pair>` now denote the current specialization (`tests/test_injected_class_name_member_default_ret0.cpp`). Nested STL `conjunction$…::value` after deferred `_Conjunction` bases can still fail in `<map>`/`<set>`/`<iostream>` when `Trait::value` is an inherited static member of a lazy trait instantiation.
- Local pointer-to-array initialization can lose its initializer and crash when indexed; array-to-pointer decay preserves inner dimensions in sema, but lowering still needs a dedicated fix.
- `tests/test_structural_class_nttp_unsupported_fail.cpp` remains an expected failure: constexpr member access on a substituted structural NTTP object is still unresolved.
