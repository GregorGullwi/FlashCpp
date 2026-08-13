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

> **Notes** column = current first stop. Blockers section below lists the mechanism to fix.
>
> The 2026-08-13 Windows/MSVC STL 14.44 sweep reran every previously failing `test_std_*.cpp` probe and the documented compile-only headers with the installed MSVC include tree. Compile-only rows use a temporary `int main()` translation unit; executable rows use `tests/run_all_tests.ps1` and MSVC `link.exe`.

| Header | Test File | Status | Notes |
|--------|-----------|--------|-------|
| `<limits>` | `test_std_limits.cpp` | ✅ Runs | 5.66s wall (retested 2026-08-13, Windows/MSVC STL 14.44). |
| `<type_traits>` | `test_std_type_traits.cpp` | ✅ Runs | 4.56s wall (retested 2026-08-02, Windows/MSVC STL 14.44). |
| `<compare>` | `test_std_compare_ret42.cpp` | ✅ Runs | 3.07s wall (retested 2026-08-02, Windows/MSVC STL 14.44). |
| `<version>` | `test_std_version.cpp` | ✅ Runs | 3.14s wall (retested 2026-08-02, Windows/MSVC STL 14.44). |
| `<source_location>` | `test_std_source_location.cpp` | ✅ Runs | 3.11s wall (retested 2026-08-02, Windows/MSVC STL 14.44). |
| `<numbers>` | N/A | ✅ Compiled | ~510ms |
| `<initializer_list>` | N/A | ✅ Compiled | ~32ms. Direct `std::initializer_list<T> values = {...}` object list-initialization is now covered by `tests/test_std_initializer_list_direct_brace_ret0.cpp` (retested 2026-04-20). |
| `<ratio>` | `test_std_ratio.cpp` | ✅ Runs | 8.55s wall (retested 2026-08-02, Windows/MSVC STL 14.44). |
| `<optional>` | `test_std_optional.cpp` | ❌ Codegen Error | 13.29s wall (retested 2026-08-13, Windows/MSVC STL 14.44). `three_way_comparable_with` is not constant; `view_interface` comparison operands then fail to resolve and `_Has_value` is missing. |
| `<any>` | `test_std_any.cpp` | ❌ Codegen Error | 13.00s wall (retested 2026-08-13, Windows/MSVC STL 14.44). `three_way_comparable_with` is not constant; `view_interface` comparison operands fail to resolve. |
| `<utility>` | `test_std_utility.cpp` | ✅ Runs | 4.74s wall (retested 2026-08-13, Windows/MSVC STL 14.44). MSVC compile, link, and runtime probe now succeed. |
| `<concepts>` | `test_std_concepts.cpp` | ✅ Runs | 4.53s wall (retested 2026-08-02, Windows/MSVC STL 14.44). |
| `<bit>` | `test_std_bit.cpp` | ✅ Runs | 5.18s wall (retested 2026-08-02, Windows/MSVC STL 14.44). |
| `<string_view>` | `test_std_string_view.cpp` | ❌ Compile Error | 14.73s wall (retested 2026-08-13, Windows/MSVC STL 14.44). `_Hash_array_representation` overload replay still fails while reparsing `_String_view_iterator::operator+`. |
| `<string>` | `test_std_string.cpp` | ❌ Compile Error | 16.14s wall (retested 2026-08-13, Windows/MSVC STL 14.44). MSVC `<xstring>` still stops with an expected identifier token diagnostic. |
| `<array>` | `test_std_array.cpp` | ❌ Codegen Error | 11.95s wall (retested 2026-08-13, Windows/MSVC STL 14.44). `three_way_comparable_with` is not constant; `view_interface` comparison operands fail to resolve. |
| `<algorithm>` | `test_std_algorithm.cpp` | ❌ Compile Error | 12.42s wall (retested 2026-08-13, Windows/MSVC STL 14.44). `_Stack_space` array bound still cannot find `_Optimistic_count` in constant expression. |
| `<span>` | `test_std_span.cpp` | ❌ Codegen Error | 11.77s wall (retested 2026-08-13, Windows/MSVC STL 14.44). `three_way_comparable_with` is not constant; `view_interface` comparison operands fail to resolve. |
| `<tuple>` | `test_std_tuple.cpp` | ✅ Runs | Retested 2026-08-13 with Windows/MSVC STL 14.44. Constructor-template partial ordering, implicit base-constructor overload resolution, zero-argument default-template deduction, and static-member definition ownership are covered by reduced non-`std` regressions. |
| `<vector>` | `test_std_vector.cpp` | ❌ Compile Error | 12.74s wall (retested 2026-08-13, Windows/MSVC STL 14.44). `vector:1537:15`: Failed to instantiate template function at `_Pocca(_Al, _Right_al)`. |
| `<deque>` | `test_std_deque.cpp` | ❌ Compile Error | 12.35s wall (retested 2026-08-13, Windows/MSVC STL 14.44). `deque:1834:6`: Missing `typename` before dependent qualified type name. |
| `<list>` | `test_std_list.cpp` | ❌ Compile Error | 13.02s wall (retested 2026-08-13, Windows/MSVC STL 14.44). `list:951:23`: Failed to instantiate template function. |
| `<queue>` | `test_std_queue.cpp` | ❌ Compile Error | 13.09s wall (retested 2026-08-13, Windows/MSVC STL 14.44). Included `<deque>` stops at `deque:1834:6`: Missing `typename` before dependent qualified type name. |
| `<stack>` | `test_std_stack.cpp` | ❌ Compile Error | 12.76s wall (retested 2026-08-13, Windows/MSVC STL 14.44). Included `<deque>` stops at `deque:1834:6`: Missing `typename` before dependent qualified type name. |
| `<memory>` | `test_std_memory.cpp` | ❌ Compile Error | 12.73s wall (retested 2026-08-13, Windows/MSVC STL 14.44). Included `<atomic>` stops at `atomic:491:84`: Ambiguous call to overloaded function `__iso_volatile_store32`. |
| `<functional>` | `test_std_functional.cpp` | ❌ Compile Error | 12.86s wall (retested 2026-08-13, Windows/MSVC STL 14.44). Included `<cmath>` stops at `cmath:1671:24`: Failed to instantiate template function. |
| `<map>` | `test_std_map.cpp` | ❌ Compile Error | 13.37s wall (retested 2026-08-13, Windows/MSVC STL 14.44). Dependent `_Seek_to` overloads still fail; instantiated `noexcept` is not a constant expression. |
| `<set>` | `test_std_set.cpp` | ❌ Compile Error | 12.60s wall (retested 2026-08-13, Windows/MSVC STL 14.44). `set:256:4`: Missing `typename` before dependent qualified type name. |
| `<ranges>` | `test_std_ranges.cpp` | ❌ Compile Error | 13.65s wall (retested 2026-08-13, Windows/MSVC STL 14.44). Sticky template-instantiation iteration limit remains. |
| `<iostream>` | `test_std_iostream.cpp` | ❌ Compile Error | 7.71s wall (retested 2026-08-13, Windows/MSVC STL 14.44). MSVC `<cmath>` `__ceilf` overload remains unresolved. |
| `<sstream>` | `test_std_sstream.cpp` | ❌ Compile Error | 7.25s wall (retested 2026-08-13, Windows/MSVC STL 14.44). MSVC `<cmath>` `__ceilf` overload remains unresolved. |
| `<fstream>` | `test_std_fstream.cpp` | ❌ Compile Error | 21.52s wall (retested 2026-08-13, Windows/MSVC STL 14.44). Included `<xstring>` stops at `xstring:3125:23`: Expected identifier token. |
| `<chrono>` | `test_std_chrono.cpp` | ❌ Compile Error | 19.61s wall (retested 2026-08-13, Windows/MSVC STL 14.44). Included `<xstring>` stops at `xstring:3125:23`: Expected identifier token. |
| `<atomic>` | `test_std_atomic.cpp` | ❌ Compile Error | 5.55s wall (retested 2026-08-13, Windows/MSVC STL 14.44). `atomic:491:84`: Ambiguous call to overloaded function `__iso_volatile_store32`. |
| `<new>` | `test_std_new.cpp` | ❌ Link Error | 5.34s wall (retested 2026-08-13, Windows/MSVC STL 14.44). Missing MSVC exception-runtime symbols remains. |
| `<exception>` | `test_std_exception.cpp` | ❌ Link Error | 5.42s wall (retested 2026-08-13, Windows/MSVC STL 14.44). Missing `__ExceptionPtrCompare` and `terminate` remains. |
| `<stdexcept>` | `test_std_stdexcept.cpp` | ❌ Compile Error | 17.17s wall (retested 2026-08-13, Windows/MSVC STL 14.44). Included `<xstring>` stops at `xstring:3125:23`: Expected identifier token. |
| `<typeinfo>` | `test_std_typeinfo_ret0.cpp` | ❌ Link Error | 4.75s wall (retested 2026-08-13, Windows/MSVC STL 14.44). Missing RTTI/exception-runtime symbols remains. |
| `<typeindex>` | `test_std_typeindex.cpp` | ✅ Compiled (compile-only) | 2.28s wall / 2.22s compiler total (retested 2026-08-02, Windows/MSVC STL 14.44). No `main`; direct compile-only probe exits 0. |
| `<numeric>` | `test_std_numeric.cpp` | ❌ Codegen Error | 11.62s wall (retested 2026-08-13, Windows/MSVC STL 14.44). `three_way_comparable_with` is not constant; `view_interface` comparison operands fail to resolve. |
| `<iterator>` | `test_std_iterator.cpp` | ❌ Compile Error | 12.45s wall (retested 2026-08-13, Windows/MSVC STL 14.44). `three_way_comparable_with` is not constant; `view_interface` comparison operands fail to resolve. |
| `<variant>` | `test_std_variant.cpp` | ❌ Compile Error | 11.91s wall (retested 2026-08-13, Windows/MSVC STL 14.44). `begin` overload deduction and dependent `_Seek_to` replay still fail. |
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
| `<latch>` | `test_std_latch.cpp` | ❌ Compile Error | 4.84s wall (retested 2026-08-13, Windows/MSVC STL 14.44). Ambiguous call to `__iso_volatile_store32` in MSVC `<atomic>`. |
| `<shared_mutex>` | `test_std_shared_mutex.cpp` | ❌ Compile Error | 13.43s wall (retested 2026-08-13, Windows/MSVC STL 14.44). Ambiguous call to `__iso_volatile_store32` in MSVC `<atomic>`. |
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
| `<cmath>` | `test_std_cmath.cpp` | ❌ Compile Error | 4.63s wall (retested 2026-08-13, Windows/MSVC STL 14.44). No matching function for `__ceilf` in MSVC `<cmath>`. |
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
| `<optional>` | Codegen: `three_way_comparable_with` is not constant; `view_interface` comparison operands fail to resolve and `_Has_value` is missing | Evaluate dependent concept/value expressions and materialize CRTP comparison members only after their concrete operands and owning layout are available |
| `<any>` | Same `three_way_comparable_with` / `view_interface` comparison stop | Same generic dependent-concept and CRTP-member materialization mechanism as `<optional>` |
| `<array>` | Same `three_way_comparable_with` / `view_interface` comparison stop | Same generic dependent-concept and CRTP-member materialization mechanism as `<optional>` |
| `<span>` | Same `three_way_comparable_with` / `view_interface` comparison stop | Same generic dependent-concept and CRTP-member materialization mechanism as `<optional>` |
| `<numeric>` | Same `three_way_comparable_with` / `view_interface` comparison stop | Same generic dependent-concept and CRTP-member materialization mechanism as `<optional>` |
| `<vector>` | Sema: `_Pocca(_Al, _Right_al)` overload/template instantiation fails | Preserve and resolve definition-bound dependent overload sets through allocator-trait member replay |
| `<string_view>` | Sema: lazy replay of `_String_view_iterator::operator+` fails after dependent `noexcept` evaluation | Complete generic lazy member-body replay for self-referential class-template members; investigate the associated dependent `_Hash_array_representation` deduction failures |
| `<iterator>` | Codegen: `three_way_comparable_with` is not constant; `view_interface` comparison operands fail to resolve | Evaluate dependent concept/value expressions and materialize CRTP comparison members only after their concrete operands and owning layout are available |
| `<ranges>` | Template-instantiation iteration limit (sticky abort) | Variadic `invoke` / CPO instantiation without SoftProbe retry storms |

The 2026-07-28 CRTP `auto&` / `view_interface::_Cast` regression is `tests/test_crtp_auto_ref_from_member_call_ret0.cpp`. Eager and lazy class-template member-body substitution now rebind pattern member-call returns (e.g. `Derived&` / `_Derived&`) through the active substitution map, attach a concrete `parser_return_type_hint`, and allow `get_expression_type` to type POI-completed dependent-unqualified calls. Local `auto`/`auto&` deduction then runs `applyPlaceholderDeclaratorDeduction` instead of leaving `TypeCategory::Auto` for the hard-use audit. This clears the shared `view_interface::empty` stop across `<optional>`, `<vector>`, `<string_view>`, `<any>`, `<span>`, `<iterator>`, and `<ranges>` without recognizing any STL helper name.

The 2026-08-11 `decltype(auto)` value-category regression is `tests/test_decltype_auto_deref_return_ret0.cpp`. Parser-time and semantic late return deduction now share sema's expression value-category result: dereferencing a pointer deduces `T&` (including aggregate references) instead of copying only the expression's object type and producing `T`. This supplies exact reference semantics before lowering rather than repairing an aggregate in codegen. Late re-deduction of placeholder returns must replay `decltype(auto)` rather than plain `auto`, or the reference is stripped again.

The 2026-08-13 member-template `auto` return regression is `tests/test_member_operator_auto_begin_struct_return_ret0.cpp`, with `if constexpr` coverage in `tests/test_if_constexpr_auto_return_discards_else_struct_ret0.cpp`, `tests/test_if_constexpr_member_operator_auto_struct_return_ret0.cpp`, and the CPO-shaped `tests/test_cpo_consteval_choice_auto_struct_return_ret0.cpp`. Instantiating a member function template used `resolve_template_type` on the `auto` return's TypeIndex, which could alias template parameter `T` and freeze the return as the range argument (16-byte struct) while the body returned an 8-byte iterator — same-size structs hid the mismatch. Placeholder `auto`/`decltype(auto)` returns are now left unsubstituted until deduction; constexpr function-local bindings feed `if constexpr` pruning; sema re-deduces placeholder returns and records the taken `if constexpr` branch for codegen.

Const/non-const `begin()` overloads on a class type must not be treated as an unresolvable member (which previously synthesized an `int` return for trailing `decltype(c.begin())`). Parser member lookup now reuses `collectConstAwareVisibleMemberFunctionCandidates`: `tests/test_trailing_decltype_const_overload_begin_not_cpo_ret0.cpp`, `tests/test_trailing_decltype_requires_begin_not_cpo_ret0.cpp`, `tests/test_trailing_decltype_member_begin_not_cpo_ret0.cpp`, and `tests/test_crtp_cpo_trailing_decltype_begin_ret0.cpp`. The shared header stop is `std::begin`/`std::end` of an instantiated `subrange`: the body is a member call named `begin`/`end` with no parent struct and a fake `int` return, while codegen types the call as `std::ranges::_Begin::_Cpo`. Reduced non-`std` copies with explicit members succeed; the remaining gap is materializing lazy/constrained `begin`/`end` on the STL `subrange` instantiation so trailing `decltype` and the body see the iterator, not a placeholder `int` or the CPO object.

The 2026-07-31 structural class-type NTTP regression is `tests/test_structural_class_nttp_ret0.cpp`. Template arguments now retain a recursive structural value identity, including nested object members, through evaluation, substitution, environment replay, specialization lookup, and mangling. The old diagnostic was a false positive caused by treating concrete class metadata as an unsupported placeholder; the reduced test now compiles, links, and runs. `tests/test_structural_class_nttp_unsupported_fail.cpp` remains an expected failure because constexpr member access on the substituted structural object is a separate unresolved evaluator path. The fresh `<optional>` probe now reaches late inherited-member/layout work instead of failing at `optional:269`.

The 2026-08-01 dependent exception-specification regressions are `tests/test_dependent_variable_template_noexcept_ret0.cpp` and `tests/test_if_constexpr_dependent_auto_return_ret0.cpp`. Constant-expression evaluation now propagates `TemplateDependentExpression` when a variable-template initializer still contains dependent type arguments, and instantiated `noexcept` substitution retains that expression until the owning template is concrete. Auto-return deduction also treats a failed `if constexpr` evaluation classified as template-dependent as a discarded/deferred branch, avoiding false conflicts between `int` and reference/pointer return types. Variable-template partial specialization matching now shares the generic dependent-member SFINAE condition inference used by other partial specializations, so an invalid `typename Type::marker` probe discards the specialization instead of selecting it. This moves `<string_view>` past its earlier `noexcept` error without any standard-library name handling. The fresh probe now exposes lazy `_String_view_iterator::operator+` replay as the next stop.

The 2026-07-27 declaration-namespace NTTP-default regression is `tests/test_class_template_later_default_nttp_ret42.cpp`; it covers both a first defaulted NTTP and a later default added on a class-template definition. Default-expression reparsing now keeps the declaration namespace active through constant evaluation, which moves `<span>` past `dynamic_extent` and into late codegen.

The 2026-07-27 nested variadic deduction regression is `tests/test_variadic_function_template_dependent_alias_return_ret42.cpp`. It covers a `get<0>`-shaped call with a mixed type pack, an inferred call, a repeated pack in two function parameters, a dependent alias return type, and a matching friend declaration. Function-template deduction now retains complete packs discovered inside template-ids, checks repeated deductions for equality, and replays both type and value substitutions while parsing the dependent return alias. The follow-on 2026-07-29 partial-spec nested typedef regression is `tests/test_dependent_alias_tuple_element_get_ret0.cpp`: `using Ttype = Tuple<This, Rest...>` on an index-0 partial must expand packs and materialize the concrete `Tuple` instantiation when registering the member alias (same mechanism as partial-spec base-class args). That clears the reduced `get`/`_Ttype` placeholder failure; the full header's next stop is deferred unmaterialized member emission.

The 2026-07-29 overload-kind regression is `tests/test_explicit_template_overload_kind_backtracking_ret42.cpp`. It declares a type-parameter overload before a non-type-parameter overload and calls the qualified overload set with an explicit integer argument. Contextual explicit-template-argument parsing now rolls back a syntactically incompatible candidate and tries the remaining declarations instead of committing to the first overload's parameter kind. This moves `<tuple>` through `std::get<0>(t)` parsing and semantic analysis without recognizing `std::get`, `tuple`, or any vendor helper name.

The 2026-07-28 dependent-local regression is `tests/test_nested_template_member_access_auto_ref_ret42.cpp`, with scope/qualifier coverage in `tests/test_dependent_auto_local_deduction_scope_ret42.cpp` (plain `auto` strips references, `const auto&` / `auto&&`, nested-block shadowing, if-init / for-init scope, mixed sizes/structs/specializations). Parser-time deduction defers when a provisional type still contains a dependent placeholder. Instantiation-time substitution carries root-local lexical bindings (`InlineVector` of declaration pointers), owner `TypeIndex` / implicit-`this` availability, and shared `applyPlaceholderDeclaratorDeduction` rather than copying expression types onto `auto` declarators. Dependence classification follows [temp.dep.expr] (member type / lookup, not blanket `NonStaticMember`; sizeof/noexcept stay non-dependent). This moves `<vector>` past `_Target->_Mypair._Myval2._Mylast` without recognizing `vector` or any vendor helper name.

The 2026-07-29 variadic conjunction/noexcept regressions are `tests/test_variadic_conjunction_value_ret42.cpp` and `tests/test_instantiated_noexcept_variadic_conjunction_ret42.cpp`. Commit `4c3bb0a1` already retained empty packs during parser replay and nested function-template deduction; the separate failure here was semantic materialization of a stored dependent-qualified-name record. `resolveDependentMemberTypeSemantic` now builds a complete `TemplateEnvironment`, retains both scalar and pack bindings (including an explicitly empty pack), and `materializeDependentRecordTemplateArgs` expands that binding instead of accidentally reusing an enclosing non-empty pack. Dependent NTTP expressions use the existing high-level `substituteTemplateParameters` path, while failed dependent/SFINAE probes remain non-fatal. A concrete invalid instantiated `noexcept` still produces a user-facing compile error.

The related dispatch regressions are `tests/test_variable_template_in_partial_spec_fixed_arg_ret42.cpp` and `tests/test_qualified_dependent_concept_in_partial_spec_ret42.cpp`. Explicit variable-template and concept-ids are no longer sent through current-owner member-function-template recovery. This keeps the missing ordered-binding check as an `InternalError` invariant instead of continuing with fabricated owner arguments, moves `<vector>` to `_Pocca` overload instantiation, and lets `<optional>` move past its structural class-type NTTP blocker to late inherited-member/layout work. `<string_view>` advances through the same machinery but still exposes a later `basic_string_view` `noexcept` constant-evaluation shape.

The empty-pack member-template overload regressions are `tests/test_empty_member_template_pack_calls_regular_overload_ret42.cpp` and `tests/std/test_member_template_variadic_template_param_not_func_pack_ret0.cpp`. The specialization was already emitted; the unresolved symbol came from late lowering replacing the parser/sema-selected ordinary overload merely because a same-name member template existed. The selected concrete declaration is now authoritative, and lowering no longer performs template deduction, instantiation, or mangling from an unqualified member name.

The 2026-08-01 conditional-expression regressions are `tests/test_ternary_string_literal_pointer_ret0.cpp`, `tests/test_ternary_glvalue_categories_review_ret0.cpp`, `tests/test_ternary_direct_xvalue_assignment_fail.cpp`, and `tests/test_ternary_xvalue_address_fail.cpp`. Semantic analysis now applies array-to-pointer decay and null-pointer-constant conversion generically, reuses the shared conversion-plan rules for pointer qualification/compatibility, and records branch conversions for codegen. It also models [expr.cond]'s value-category rule: same-type lvalue operands remain lvalues, same-type xvalue operands remain xvalues, and mixed categories produce prvalues. Codegen now consumes the sema-selected overload and stores through an address-only conditional lvalue; direct assignment to a conditional xvalue and built-in address-of on a conditional xvalue are rejected. This removes the generic `Sema-normalized ternary expression missing exact result type` stop without special-casing `<exception>` or `<typeindex>`.

The 2026-08-10 declaration-namespace lookup regression is `tests/test_ternary_constexpr_size_alias_global_ret0.cpp`. It reduces `_Meta_find_unique_index_i_2` to ordinary non-`std` code and covers an alias-backed 64-bit result, a namespace-scope constant, a qualified constexpr helper call, an enclosing-namespace lookup from a nested namespace, and a templated aggregate consumer. Semantic normalization now retains the function definition's namespace and performs ordinary lookup through that namespace and its parents after local lookup. This lets both conditional branches acquire the same exact `Size` type and removes the shared `Sema-normalized ternary expression missing exact result type` failure from `<array>`, `<span>`, `<numeric>`, `<iterator>`, `<optional>`, and `<any>`. The downstream `Phase 15: sema missed variable init conversion (int -> long long)` diagnostic for `_Threshold_find_first_of` also disappears; it was exposed after the failed namespace-scoped function interrupted codegen state rather than being the next independent language blocker.

A follow-up probe with pointers to inner arrays exposed a separate existing codegen gap: local pointer-to-array initialization currently loses its initializer and can crash when indexed. The semantic array-decay helper preserves inner dimensions according to [conv.array], but pointer-to-array lowering still needs its own regression and fix before that form is claimed as supported.

Regressions for the 2026-07-26 Windows fixes: `tests/test_template_member_type_prefers_parameter_spelling_ret0.cpp`, `tests/test_interleaved_declspec_allocator_ret42.cpp`, `tests/test_interleaved_declspec_cv_qualifiers_ret42.cpp`, `tests/test_interleaved_declspec_const_assignment_fail.cpp`, and `tests/test_using_bare_function_type_calling_convention_ret42.cpp`. The shared declaration-specifier parser now carries `const`/`volatile` across interleaved Microsoft `__declspec` specifiers, and semantic analysis rejects assignment to the resulting const-qualified object. The `<ratio>` definition-context/deferred-base regressions are `tests/test_dependent_base_static_member_multi_expr_ret0.cpp` and `tests/std/test_std_ratio_equal_only.cpp`. Regressions for class-template layout/friend fixes: `tests/test_class_tmpl_cross_spec_complete_layout_ret0.cpp`, `tests/test_class_tmpl_friend_plus_byvalue_ret0.cpp`. Regressions for recent `<limits>` fixes: `tests/test_unused_inline_no_emit_ret0.cpp`, `tests/test_used_inline_still_emitted_ret0.cpp`, `tests/test_selectany_comdat_ret0.cpp`, plus earlier typedef-assign / rvalue-assign tests. Template OOL / partial-spec member cv regressions: `tests/test_template_partial_spec_ool_ctor_template_qualified_name_ret0.cpp`, `tests/test_template_partial_spec_namespaced_ool_ctor_template_same_name_overload_ret0.cpp`, and `tests/test_partial_spec_member_const_overload_ret0.cpp` (injected-class-name OOL ctor recognition + preserved trailing `const` on partial-spec members so `_Get_rest`-style overloads are not ambiguous).
