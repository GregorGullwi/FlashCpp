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

| Header | Test File | Status | Notes |
|--------|-----------|--------|-------|
| `<limits>` | `test_std_limits.cpp` | ✅ Compiled | ~5525ms (`TOTAL`) / ~6.1s wall (retested 2026-07-29, Windows/MSVC STL 14.44). |
| `<type_traits>` | `test_std_type_traits.cpp` | ✅ Compiled | ~1461ms (`TOTAL`) / ~1.5s wall (retested 2026-07-29, Windows/MSVC STL 14.44). |
| `<compare>` | `test_std_compare_ret42.cpp` | ✅ Compiled | ~0.06s (retested 2026-05-23, Linux/libstdc++-14). |
| `<version>` | `test_std_version.cpp` | ✅ Compiled | ~41ms |
| `<source_location>` | `test_std_source_location.cpp` | ✅ Compiled | ~41ms |
| `<numbers>` | N/A | ✅ Compiled | ~510ms |
| `<initializer_list>` | N/A | ✅ Compiled | ~32ms. Direct `std::initializer_list<T> values = {...}` object list-initialization is now covered by `tests/test_std_initializer_list_direct_brace_ret0.cpp` (retested 2026-04-20). |
| `<ratio>` | `test_std_ratio.cpp` | ✅ Compiled | ~5.4–5.7s (`TOTAL`/wall, retested 2026-07-29, Windows/MSVC STL 14.44). Definition-context lookup now preserves namespace-scope constexpr calls during template replay, and deferred base aliases resolve in their declaration namespace. Regressions: `tests/test_dependent_base_static_member_multi_expr_ret0.cpp` and `tests/std/test_std_ratio_equal_only.cpp`. |
| `<optional>` | `test_std_optional.cpp` | ❌ Codegen Error | ~12.3s runner wall (retested 2026-08-01, Windows/MSVC STL 14.44). Structural class-type NTTP parsing and identity now pass; the first hard stops are late aggregate-layout diagnostics followed by a missing inherited member `_Has_value` in an instantiated `optional` specialization. |
| `<any>` | `test_std_any.cpp` | ❌ Codegen Error | ~12.8s runner wall (retested 2026-08-01, Windows/MSVC STL 14.44). Stops on aggregate-layout metadata, then a missing resolved tuple constructor and an unresolved callable `swap` operation. |
| `<utility>` | `test_std_utility.cpp` | ✅ Runs | ~1.8–2.0s (`TOTAL`/wall, retested 2026-07-29, Windows/MSVC STL 14.44). `pair<int, float>` now links and returns 0. Address-valued IR copies use the 64-bit GPR path even when the pointee is floating-point, fixing out-of-line `float&&` returns. Explicit-template-argument materialization marks only structurally proven single-parameter reference identities for the `inline_always` address rewrite; both `forward` calls inline, while unrelated reference returns retain their C++ semantics. Regressions: `tests/test_float_rvalue_ref_return_ret0.cpp`, `tests/test_float_rvalue_ref_cast_noinline_ret0.cpp`, `tests/test_template_pure_expr_rvalue_ref_ret0.cpp`, `tests/test_template_pure_expr_non_identity_ref_ret0.cpp`, and `tests/test_inline_member_template_non_identity_ref_ret0.cpp`. Header probe: `tests/std/test_std_utility.cpp`. |
| `<concepts>` | `test_std_concepts.cpp` | ✅ Compiled | ~1.4–1.5s (`TOTAL`/wall, retested 2026-07-29, Windows/MSVC STL 14.44). |
| `<bit>` | `test_std_bit.cpp` | ✅ Compiled | ~1083ms (retested 2026-05-23, Linux/libstdc++-14). |
| `<string_view>` | `test_std_string_view.cpp` | ❌ Compile Error | ~15.0s runner wall (retested 2026-08-01, Windows/MSVC STL 14.44). Dependent `noexcept` evaluation now gets past the previous hard stop; the first remaining error is lazy replay of `_String_view_iterator::operator+`, with non-fatal `_Hash_array_representation` overload-probe noise. |
| `<string>` | `test_std_string.cpp` | ❌ Compile Error | ~6.19s (`TOTAL`) / ~6.68s wall (retested 2026-05-27, Linux/libstdc++-14). Completed class-template cache hits no longer consume template-depth budget; current first hard error is now depth-guarded recursive `basic_string` instantiation. |
| `<array>` | `test_std_array.cpp` | ✅ Compiled | ~2.64s (retested 2026-05-23, Linux/libstdc++-14). |
| `<algorithm>` | `test_std_algorithm.cpp` | 💥 Crash | ~4.95s (retested 2026-05-21, Linux/libstdc++-14). The shared `ptr_traits` member-alias-template target now parses; current run reaches late IR/codegen (`std::partial_ordering` missing resolved constructor / unresolved semantic type category 25) and can still crash after deep template replay. |
| `<span>` | `test_std_span.cpp` | ❌ Codegen Error | ~12.1s runner wall (retested 2026-08-01, Windows/MSVC STL 14.44). Stops on aggregate-layout metadata during code generation. |
| `<tuple>` | `test_std_tuple.cpp` | ❌ Codegen Error | ~5.7s runner wall (retested 2026-08-01, Windows/MSVC STL 14.44). Stops on missing resolved constructors and a deferred function queue entry that remains unmaterialized. |
| `<vector>` | `test_std_vector.cpp` | ❌ Compile Error | ~12.9s runner wall (retested 2026-08-01, Windows/MSVC STL 14.44). Stops while instantiating `_Pocca` at `vector:1537:15`. |
| `<deque>` | `test_std_deque.cpp` | 💥 Crash | ~2464ms (retested 2026-04-11). |
| `<list>` | `test_std_list.cpp` | ❌ Compile Error | ~2940ms (retested 2026-05-12, Linux/libstdc++-14). The shared `_Head_base` default-NTTP stop remains fixed; after raising template nesting limits the first hard error is still depth-guarded, now `Max template instantiation depth (40) exceeded for 'polymorphic_allocator'`. |
| `<queue>` | `test_std_queue.cpp` | 💥 Crash | ~2522ms (retested 2026-04-11). |
| `<stack>` | `test_std_stack.cpp` | 💥 Crash | ~2464ms (retested 2026-04-11). |
| `<memory>` | `test_std_memory.cpp` | ❌ Compile Error | ~6.64s (`TOTAL`) / ~7.15s wall (retested 2026-05-27, Linux/libstdc++-14). `__make_move_if_noexcept_iterator` still emits non-fatal overload noise, but the current first hard error remains depth-guarded recursive `basic_string` instantiation. |
| `<functional>` | `test_std_functional.cpp` | ❌ Compile Error | ~4.76s (`TOTAL`) / ~5.13s wall (retested 2026-05-27, Linux/libstdc++-14). `__make_move_if_noexcept_iterator` still emits non-fatal overload noise, but the current first hard error remains depth-guarded `rebind`. |
| `<map>` | `test_std_map.cpp` | ❌ Compile Error | ~2498ms (retested 2026-04-30, Linux/libstdc++-14). No longer stops at `Missing TypeInfo while computing template argument size`; it now reaches `Unregistered dependent placeholder type reached template argument classification`. |
| `<set>` | `test_std_set.cpp` | ❌ Compile Error | ~2350ms (retested 2026-04-12). The earlier variable-template/type-traits arity blocker is gone. Current first error is later in the Windows UCRT headers: "No matching function for call to '__stdio_common_vfwprintf'". |
| `<ranges>` | `test_std_ranges.cpp` | ❌ Compile Error | ~14.96s runner wall (retested 2026-07-31, Windows/MSVC STL 14.44). Stops at the existing sticky template-instantiation iteration limit. |
| `<iostream>` | `test_std_iostream.cpp` | 💥 Crash | ~4559ms (retested 2026-04-11). |
| `<sstream>` | `test_std_sstream.cpp` | 💥 Crash | ~4565ms (retested 2026-04-11). |
| `<fstream>` | `test_std_fstream.cpp` | 💥 Crash | ~4642ms (retested 2026-04-11). |
| `<chrono>` | `test_std_chrono.cpp` | ❌ Compile Error | ~6638ms (retested 2026-04-11). Call to deleted function 'swap'. |
| `<atomic>` | `test_std_atomic.cpp` | ✅ Compiled | ~838ms (retested 2026-04-24, Linux/libstdc++). **NEW: Now compiles successfully on Linux!** Previous deferred member function codegen errors are resolved. |
| `<new>` | `test_std_new.cpp` | ✅ Compiled | ~56ms |
| `<exception>` | `test_std_exception.cpp` | ✅ Compiled | ~368ms (retested 2026-04-24, Linux/libstdc++). **NEW: Now compiles successfully on Linux!** The `exception_ptr` copy-vs-move-constructor ambiguity is resolved by the rvalue overload-rank fix. Regression: `tests/test_rvalue_ref_overload_preference_ret0.cpp`. |
| `<stdexcept>` | `test_std_stdexcept.cpp` | ❌ Compile Error | ~5.95s (`TOTAL`) / ~6.42s wall (retested 2026-05-25, Linux/libstdc++-14). First hard error now matches `<memory>/<string>`: `Could not evaluate non-type template default for parameter 1 of '__hash_enum'`. |
| `<typeinfo>` | `test_std_typeinfo_ret0.cpp` | ✅ Compiled | ~46ms (retested 2026-04-30, Linux/libstdc++-14). Sema now models pointer arithmetic (`T* + integral`, `T* - integral`, `T* - T*`) so the ternary in `type_info::name()` (`__name[0] == '*' ? __name + 1 : __name`) gets a sema-owned exact result type and codegen no longer throws. Regression: `tests/test_ternary_pointer_arithmetic_branches_ret0.cpp`. |
| `<typeindex>` | `test_std_typeindex.cpp` | ✅ Compiled (compile-only) | ~2348ms wall / ~2285ms compiler total (retested 2026-08-01, Windows/MSVC STL 14.44). Sema now models the array-to-pointer conditional in `exception::what()` and supplies codegen's exact ternary result type. The compile-only probe exits 0; a linked `main` probe is separately blocked by missing MSVC RTTI/exception runtime symbols. |
| `<numeric>` | `test_std_numeric.cpp` | ✅ Compiled | ~7529ms (retested 2026-05-25, Linux/libstdc++-14). **NOW WORKS**: ternary common-type fix resolved `numeric_limits` member constexpr folding. Builtin `__builtin_huge_val`/`__builtin_nan` families now handled in constexpr evaluator. |
| `<iterator>` | `test_std_iterator.cpp` | ❌ Codegen Error | ~12.2s runner wall (retested 2026-08-01, Windows/MSVC STL 14.44). Stops on aggregate-layout metadata during code generation. |
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
| `<optional>` | Sema/codegen: late aggregate layout followed by missing inherited member `_Has_value` | Complete deferred/inherited base materialization and canonical aggregate layout metadata |
| `<any>` | Codegen: Phase 15 init conversion / unique-id / pack-expansion IR | Exact result types, complete aggregate layout, and pack expansion before codegen |
| `<span>` | Codegen: sema ternary exact result type; Phase 15 `int`→`long long` | Exact comparison/ternary types and complete canonical aggregate layout |
| `<tuple>` | Codegen: deferred queue receives an unmaterialized tuple member function/constructor after `get` / `_Ttype` materialization; residual `swap` emission gaps | Materialize deferred member definitions before enqueue; finish constructor/`swap` emission |
| `<vector>` | Sema: `_Pocca(_Al, _Right_al)` overload/template instantiation fails | Preserve and resolve definition-bound dependent overload sets through allocator-trait member replay |
| `<string_view>` | Sema: lazy replay of `_String_view_iterator::operator+` fails after dependent `noexcept` evaluation | Complete generic lazy member-body replay for self-referential class-template members; investigate the associated dependent `_Hash_array_representation` deduction failures |
| `<iterator>` | Codegen: sema ternary / Phase 15 / `begin`/`end` lowering | Exact result types for ternary/return; complete types for CPO/`auto` results in ranges interface members |
| `<ranges>` | Template-instantiation iteration limit (sticky abort) | Variadic `invoke` / CPO instantiation without SoftProbe retry storms |

The 2026-07-28 CRTP `auto&` / `view_interface::_Cast` regression is `tests/test_crtp_auto_ref_from_member_call_ret0.cpp`. Eager and lazy class-template member-body substitution now rebind pattern member-call returns (e.g. `Derived&` / `_Derived&`) through the active substitution map, attach a concrete `parser_return_type_hint`, and allow `get_expression_type` to type POI-completed dependent-unqualified calls. Local `auto`/`auto&` deduction then runs `applyPlaceholderDeclaratorDeduction` instead of leaving `TypeCategory::Auto` for the hard-use audit. This clears the shared `view_interface::empty` stop across `<optional>`, `<vector>`, `<string_view>`, `<any>`, `<span>`, `<iterator>`, and `<ranges>` without recognizing any STL helper name.

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

A follow-up probe with pointers to inner arrays exposed a separate existing codegen gap: local pointer-to-array initialization currently loses its initializer and can crash when indexed. The semantic array-decay helper preserves inner dimensions according to [conv.array], but pointer-to-array lowering still needs its own regression and fix before that form is claimed as supported.

Regressions for the 2026-07-26 Windows fixes: `tests/test_template_member_type_prefers_parameter_spelling_ret0.cpp`, `tests/test_interleaved_declspec_allocator_ret42.cpp`, `tests/test_interleaved_declspec_cv_qualifiers_ret42.cpp`, `tests/test_interleaved_declspec_const_assignment_fail.cpp`, and `tests/test_using_bare_function_type_calling_convention_ret42.cpp`. The shared declaration-specifier parser now carries `const`/`volatile` across interleaved Microsoft `__declspec` specifiers, and semantic analysis rejects assignment to the resulting const-qualified object. The `<ratio>` definition-context/deferred-base regressions are `tests/test_dependent_base_static_member_multi_expr_ret0.cpp` and `tests/std/test_std_ratio_equal_only.cpp`. Regressions for class-template layout/friend fixes: `tests/test_class_tmpl_cross_spec_complete_layout_ret0.cpp`, `tests/test_class_tmpl_friend_plus_byvalue_ret0.cpp`. Regressions for recent `<limits>` fixes: `tests/test_unused_inline_no_emit_ret0.cpp`, `tests/test_used_inline_still_emitted_ret0.cpp`, `tests/test_selectany_comdat_ret0.cpp`, plus earlier typedef-assign / rvalue-assign tests. Template OOL / partial-spec member cv regressions: `tests/test_template_partial_spec_ool_ctor_template_qualified_name_ret0.cpp`, `tests/test_template_partial_spec_namespaced_ool_ctor_template_same_name_overload_ret0.cpp`, and `tests/test_partial_spec_member_const_overload_ret0.cpp` (injected-class-name OOL ctor recognition + preserved trailing `const` on partial-spec members so `_Get_rest`-style overloads are not ambiguous).
