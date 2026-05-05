# Standard Header Tests

This directory contains test files for C++ standard library headers to assess FlashCpp's compatibility with the C++ standard library.

## Current Status

> **Note (2026-05-04 injected-class-name copy-init follow-up):** the rows below still include historical timings from earlier hosts and commits.  The authoritative current Linux/libstdc++-14 state for the retested `tests/std/test_std_*.cpp` files is the latest dated section below the table.

| Header | Test File | Status | Notes |
|--------|-----------|--------|-------|
| `<limits>` | `test_std_limits.cpp` | ✅ Compiled | ~1418ms (retested 2026-05-04, Linux/libstdc++-14); wchar_t/char32_t Phase 15 blocker fixed. See latest dated section. |
| `<type_traits>` | `test_std_type_traits.cpp` | ✅ Compiled | ~351ms (retested 2026-05-04, Linux/libstdc++-14). Member type aliases now preserve substituted template-argument pointer/reference/cv modifiers, so `std::is_pointer<int*>::value` succeeds. Regression: `tests/test_template_member_alias_preserves_pointer_ret0.cpp`. See latest dated section. |
| `<compare>` | `test_std_compare_ret42.cpp` | ❌ Codegen Error | ~609ms (retested 2026-04-11). Targeted test still compiles, but bare `#include <compare>` now fails with "Ambiguous constructor call" during codegen of a namespace-level node. |
| `<version>` | `test_std_version.cpp` | ✅ Compiled | ~41ms |
| `<source_location>` | `test_std_source_location.cpp` | ✅ Compiled | ~41ms |
| `<numbers>` | N/A | ✅ Compiled | ~510ms |
| `<initializer_list>` | N/A | ✅ Compiled | ~32ms. Direct `std::initializer_list<T> values = {...}` object list-initialization is now covered by `tests/test_std_initializer_list_direct_brace_ret0.cpp` (retested 2026-04-20). |
| `<ratio>` | `test_std_ratio.cpp` | ✅ Compiled | ~639ms. The header still compiles, but `std::ratio_less` remains blocked because non-type default template arguments that depend on qualified constexpr members (for example `__ratio_less_impl`'s bool defaults) are still not fully instantiated/evaluated. |
| `<optional>` | `test_std_optional.cpp` | ❌ Compile Error | ~1248ms (retested 2026-05-04, Linux/libstdc++-14). The earlier MSVC `<utility>:82` parse stop and `Cannot use copy initialization with explicit constructor` diagnostic are both fixed; current first hard error is `Itanium name mangling: unresolved 'auto' type reached mangling` for `__begin`. |
| `<any>` | `test_std_any.cpp` | ❌ Codegen Error | ~607ms (retested 2026-04-11). Targeted test now fails with "Expected symbol '_Arg' to exist in code generation" in `std::any` constructor. |
| `<utility>` | `test_std_utility.cpp` | ✅ Compiled | ~587ms (retested 2026-04-28, Linux/libstdc++-14). The dependent `decltype` alias target in `__do_common_type_impl::__cond_t` is no longer collapsed to concrete `auto`, so the full targeted header compiles. Regression: `tests/test_dependent_decltype_alias_template_ret0.cpp`. |
| `<concepts>` | `test_std_concepts.cpp` | ✅ Compiled | ~1518ms (retested 2026-04-20). The line 254 requires-expression pack expansion blocker is fixed by `tests/test_std_concepts_pack_expansion_ret42.cpp`. The compile still logs recoverable `is_integral_v` instantiation warnings, tracked separately under `<type_traits>`. |
| `<bit>` | `test_std_bit.cpp` | ✅ Compiled | ~625ms |
| `<string_view>` | `test_std_string_view.cpp` | ❌ Compile Error | ~2585ms (retested 2026-05-04, Linux/libstdc++-14). The explicit-ctor copy-init diagnostic for `std::ranges::__detail::__max_size_type` is fixed; current first hard error is `cannot initialize a variable of type 'struct' with an lvalue of type 'const char[N]'` deeper in the header. |
| `<string>` | `test_std_string.cpp` | ❌ Compile Error | ~8484ms (retested 2026-05-04, Linux/libstdc++-14). Same explicit-ctor unblock as `<string_view>`; current first error is `cannot initialize a variable of type 'struct' with an lvalue of type 'const char[N]'`. |
| `<array>` | `test_std_array.cpp` | ❌ Codegen Error | Focused retest 2026-05-04 after injected-class-name fix. The `Cannot use copy initialization with explicit constructor` diagnostic for `std::reverse_iterator` is fixed; the header now progresses into existing IR/codegen gaps around unresolved `std::reverse_iterator` constructor/placeholder lowering. |
| `<algorithm>` | `test_std_algorithm.cpp` | ❌ Compile Error | ~2758ms (retested 2026-05-04, Linux/libstdc++-14). Same explicit-ctor unblock as `<array>`; current first error is `Itanium name mangling: unresolved 'auto' type reached mangling` for `__begin`. |
| `<span>` | `test_std_span.cpp` | ✅ Compiled | ~41ms (retested 2026-04-11). **NEW: Now compiles successfully!** Previous iterator/ranges codegen blockers are resolved. |
| `<tuple>` | `test_std_tuple.cpp` | ❌ Compile Error | ~2262ms (retested 2026-04-24, Linux/libstdc++). Previous `tuple:399` `_M_tail` blocker resolved by the member-function overload registration fix; now first-order error is `unsupported PackExpansionExprNode reached semantic analysis` deeper in tuple's pack expansion machinery. |
| `<vector>` | `test_std_vector.cpp` | ❌ Compile Error | ~2062ms (retested 2026-04-30, Linux/libstdc++-14). No longer stops at `Missing TypeInfo while computing template argument size`; it now reaches `Itanium name mangling: unknown type — cannot generate valid symbol` after several deferred/incomplete `reverse_iterator` instantiations. |
| `<deque>` | `test_std_deque.cpp` | 💥 Crash | ~2464ms (retested 2026-04-11). |
| `<list>` | `test_std_list.cpp` | ❌ Compile Error | ~2999ms (retested 2026-04-24, Linux/libstdc++). `_M_tail` blocker resolved by the member-function overload fix; now first-order error is `unsupported PackExpansionExprNode reached semantic analysis` deeper in shared tuple pack-expansion code. |
| `<queue>` | `test_std_queue.cpp` | 💥 Crash | ~2522ms (retested 2026-04-11). |
| `<stack>` | `test_std_stack.cpp` | 💥 Crash | ~2464ms (retested 2026-04-11). |
| `<memory>` | `test_std_memory.cpp` | ❌ Compile Error | ~3067ms (retested 2026-04-30, Linux/libstdc++-14). No longer stops at `Missing TypeInfo while computing template argument size`; it now reaches `ExpressionSubstitutor missing binding for ordered template parameter '_Head'` in tuple machinery. |
| `<functional>` | `test_std_functional.cpp` | ❌ Compile Error | ~3763ms (retested 2026-04-24, Linux/libstdc++). `_M_tail` blocker resolved by the member-function overload fix; now first-order error is non-dependent name `__node_gen` C++20 [temp.res]/9 violation (false positive triggered by template reparse depth-guard firing during deep instantiation). |
| `<map>` | `test_std_map.cpp` | ❌ Compile Error | ~2498ms (retested 2026-04-30, Linux/libstdc++-14). No longer stops at `Missing TypeInfo while computing template argument size`; it now reaches `Unregistered dependent placeholder type reached template argument classification`. |
| `<set>` | `test_std_set.cpp` | ❌ Compile Error | ~2350ms (retested 2026-04-12). The earlier variable-template/type-traits arity blocker is gone. Current first error is later in the Windows UCRT headers: "No matching function for call to '__stdio_common_vfwprintf'". |
| `<ranges>` | `test_std_ranges.cpp` | ❌ Compile Error | ~2906ms (retested 2026-04-12). The earlier variable-template/type-traits arity blocker is gone. Current first error is later in the Windows UCRT headers: "No matching function for call to '__stdio_common_vfwprintf'". |
| `<iostream>` | `test_std_iostream.cpp` | 💥 Crash | ~4559ms (retested 2026-04-11). |
| `<sstream>` | `test_std_sstream.cpp` | 💥 Crash | ~4565ms (retested 2026-04-11). |
| `<fstream>` | `test_std_fstream.cpp` | 💥 Crash | ~4642ms (retested 2026-04-11). |
| `<chrono>` | `test_std_chrono.cpp` | ❌ Compile Error | ~6638ms (retested 2026-04-11). Call to deleted function 'swap'. |
| `<atomic>` | `test_std_atomic.cpp` | ✅ Compiled | ~838ms (retested 2026-04-24, Linux/libstdc++). **NEW: Now compiles successfully on Linux!** Previous deferred member function codegen errors are resolved. |
| `<new>` | `test_std_new.cpp` | ✅ Compiled | ~56ms |
| `<exception>` | `test_std_exception.cpp` | ✅ Compiled | ~368ms (retested 2026-04-24, Linux/libstdc++). **NEW: Now compiles successfully on Linux!** The `exception_ptr` copy-vs-move-constructor ambiguity is resolved by the rvalue overload-rank fix. Regression: `tests/test_rvalue_ref_overload_preference_ret0.cpp`. |
| `<stdexcept>` | `test_std_stdexcept.cpp` | ❌ Compile Error | ~9408ms (retested 2026-05-04, Linux/libstdc++-14). No longer crashes; the `Cannot use copy initialization with explicit constructor` diagnostic is also fixed; current first hard error is `Itanium name mangling: unresolved 'auto' type reached mangling` for `__begin`. |
| `<typeinfo>` | `test_std_typeinfo_ret0.cpp` | ✅ Compiled | ~46ms (retested 2026-04-30, Linux/libstdc++-14). Sema now models pointer arithmetic (`T* + integral`, `T* - integral`, `T* - T*`) so the ternary in `type_info::name()` (`__name[0] == '*' ? __name + 1 : __name`) gets a sema-owned exact result type and codegen no longer throws. Regression: `tests/test_ternary_pointer_arithmetic_branches_ret0.cpp`. |
| `<typeindex>` | N/A | ❌ Codegen Error | ~640ms (retested 2026-04-11). "Cannot use copy initialization with explicit constructor". |
| `<numeric>` | `test_std_numeric.cpp` | ❌ Compile Error | ~2411ms (retested 2026-05-04, Linux/libstdc++-14). The `Cannot use copy initialization with explicit constructor` diagnostic is fixed; current first error is `Itanium name mangling: unresolved 'auto' type reached mangling` for `__begin`. |
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



#### 2026-05-05 Copy-init array-to-pointer constructor conversion for `<string_view>` (Linux/libstdc++-14)

This pass rebuilt `x64/Sharded/FlashCpp` with clang++ and retested every
`tests/std/test_std_*.cpp` file against libstdc++-14.

Fix landed:

- **Copy-initialized converting constructors now consider C++20 [conv.array]
  array-to-pointer decay for one-dimensional array prvalues/lvalues that are not
  already represented as pointer arguments.**  This keeps the fix in overload
  conversion planning and sema's selected-constructor annotation path, allowing
  `View v = "abc";` and the `std::basic_string_view(const CharT*)` path to select
  the non-explicit constructor instead of falling through to the invalid
  array-to-struct diagnostic.

Regression test:

- `tests/test_copy_init_ctor_array_to_pointer_ret0.cpp`

Validation snapshot:

- Full Linux regression suite (`bash tests/run_all_tests.sh`): 2276 pass / 0 fail
  / 156 `_fail` correct.
- Existing array-decay guards rechecked:
  `test_array_decay_pointer_metadata_ret0.cpp`, `test_array_pass_simple_ret42.cpp`,
  `test_puts_stack_ret0.cpp`, and `test_toplevel_const_ptr_arg_ret0.cpp`.

Linux/libstdc++-14 std-header sweep (`x64/Sharded/FlashCpp`, 60 s timeout):

| Header | Status | Time | First-order stop / note |
|--------|--------|------|-------------------------|
| `<aggregate_brace_elision_follow>` | ✅ Compiled | 48ms | |
| `<bit>` | ✅ Compiled | 626ms | |
| `<compare_ret42>` | ✅ Compiled | 35ms | |
| `<concepts>` | ✅ Compiled | 539ms | |
| `<exception>` | ✅ Compiled | 563ms | |
| `<limits>` | ✅ Compiled | 1645ms | |
| `<new>` | ✅ Compiled | 62ms | |
| `<pair_swap_deleted_member>` | ✅ Compiled | 29ms | |
| `<rel_ops_no_false_instantiation_ret0>` | ✅ Compiled | 864ms | |
| `<source_location>` | ✅ Compiled | 43ms | |
| `<span>` | ✅ Compiled | 45ms | |
| `<type_traits>` | ✅ Compiled | 458ms | |
| `<type_traits_is_integral_any_of_fail>` | ✅ Compiled | 20ms | |
| `<typeinfo_ret0>` | ✅ Compiled | 60ms | |
| `<utility>` | ✅ Compiled | 863ms | |
| `<version>` | ✅ Compiled | 43ms | |
| `<algorithm>` | ❌ Compile Error | 2568ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<any>` | ❌ Compile Error | 622ms | `Ambiguous constructor call` in `main`. |
| `<array>` | ❌ Compile Error | 1564ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<atomic>` | ❌ Compile Error | 972ms | `Fatal error: Base class instantiation name should resolve after default filling`. |
| `<chrono>` | ❌ Compile Error | 3816ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<cmath>` | ❌ Compile Error | 6307ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<deque>` | ❌ Compile Error | 2518ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<fstream>` | ❌ Compile Error | 8578ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<functional>` | ❌ Compile Error | 2463ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<iostream>` | ❌ Compile Error | 8596ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<iterator>` | ❌ Compile Error | 2968ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<latch>` | ❌ Compile Error | 966ms | `Fatal error: Base class instantiation name should resolve after default filling`. |
| `<list>` | ❌ Compile Error | 2839ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<map>` | ❌ Compile Error | 2587ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<memory>` | ❌ Compile Error | 3174ms | `sizeof evaluated to 0 for type '' (incomplete or dependent type) - cannot allocate incomplete types`. |
| `<numeric>` | ❌ Compile Error | 2621ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<optional>` | ❌ Compile Error | 1315ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<optional_codegen_recovery>` | ❌ Compile Error | 1313ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<queue>` | ❌ Compile Error | 2656ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<ranges>` | ❌ Compile Error | 3152ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<ratio>` | ❌ Compile Error | 637ms | `ratio_less::value` is still not a constant expression. |
| `<set>` | ❌ Compile Error | 2611ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<shared_mutex>` | ❌ Compile Error | 2585ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<sstream>` | ❌ Compile Error | 8645ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<stack>` | ❌ Compile Error | 2583ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<stdexcept>` | ❌ Compile Error | 8587ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<string>` | ❌ Compile Error | 8559ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<string_view>` | ❌ Compile Error | 2935ms | The previous `const char[N]` copy-init blocker is fixed; current first-order stop is `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<tuple>` | ❌ Compile Error | 1970ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<variant>` | ❌ Compile Error | 2900ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<vector>` | ❌ Compile Error | 2122ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |
| `<wstring_view_find_ret0>` | ❌ Compile Error | 2650ms | `Fold expression requires template instantiation context - each argument type must be a complete class or an unbounded array`. |

The broad next blocker is now the fold-expression completeness check reported
from template instantiation across many range/iterator-heavy headers.  Two
independent blockers remain visible in this sweep: default-filled base-class
instantiation for `<atomic>`/`<latch>`, and the existing `ratio_less::value`
constant-expression gap in `<ratio>`.

#### 2026-05-04 Injected-class-name copy-init follow-up for `<array>` / `std::reverse_iterator` (Linux/libstdc++-14)

Fix landed:

- **Injected-class-name type canonicalization in instantiated class-template member contexts.**  Inside members of a class template specialization, a local declaration such as `ReverseLike copy = *this;` now canonicalizes the injected-class-name type to the current specialization.  This prevents same-type copy-initialization from being misclassified as a converting construction through an explicit iterator constructor.

Regression test:

- `tests/test_template_injected_class_name_copy_init_ret0.cpp`

Validation snapshot:

- Full Linux regression suite (`bash tests/run_all_tests.sh`): 2272 pass / 0 fail / 154 `_fail` correct.
- Focused std-header probes before this fix, using the same rebuilt sharded binary, confirmed these current timings and first-order stops: `<string_view>` 1910ms (`cannot initialize ... const char[14]`), `<optional>` 900ms (no grep-captured first-order diagnostic), `<stdexcept>` 5690ms (`Ambiguous constructor call`), `<numeric>` 1820ms (`Itanium name mangling: unresolved 'auto' type reached mangling` for `__begin`), and `<tuple>` 1370ms (`ExpressionSubstitutor missing binding for ordered template parameter '_Head'`).
- `<array>` was 1050ms before the fix and stopped at the now-fixed `Cannot use copy initialization with explicit constructor` diagnostic.  The post-fix focused probe progresses past semantic analysis into existing IR/codegen gaps around missing resolved constructor metadata for `std::reverse_iterator` and unresolved placeholder return lowering.

The broad next blockers are now less dominated by explicit-constructor copy-init false positives in iterator headers; the remaining failures in this slice point at constructor metadata handoff, placeholder/`auto` lowering, tuple substitution bindings, and unrelated overload ambiguity.

#### 2026-05-04 Member type-alias modifier preservation for `<type_traits>` (Linux/libstdc++-14)

This pass rebuilt `x64/Sharded/FlashCpp` with clang++ and retested every
`tests/std/test_std_*.cpp` file against libstdc++-14.

Fix landed:

- **Instantiated member type aliases whose target is a template parameter now
  preserve the concrete argument's pointer/reference/cv modifiers.**  In
  libstdc++ `<type_traits>`, `std::remove_cv<int*>::type` previously registered
  as plain `int` because alias registration kept only the substituted
  `TypeIndex` category and dropped the `TemplateTypeArg` modifiers.  That made
  `__is_pointer_helper<__remove_cv_t<int*>>` select the primary `false_type`
  template instead of the `T*` partial specialization.  Alias registration now
  rebinds direct template-parameter member aliases through `TemplateTypeArg`,
  while leaving function-pointer aliases on the existing signature-preserving
  path.

Regression tests:

- `tests/test_template_member_alias_preserves_pointer_ret0.cpp`
- Existing guard rechecked: `tests/test_template_alias_funcptr_ret0.cpp`

Full Linux regression suite (`bash tests/run_all_tests.sh`): 2270 pass / 0 fail
/ 154 `_fail` correct.

Linux/libstdc++-14 std-header sweep (`x64/Sharded/FlashCpp`, 60 s timeout):

| Header | Status | Time | First-order stop / note |
|--------|--------|------|-------------------------|
| `<aggregate_brace_elision_follow>` | ✅ Compiled | 38ms | |
| `<bit>` | ✅ Compiled | 487ms | |
| `<compare_ret42>` | ✅ Compiled | 26ms | |
| `<concepts>` | ✅ Compiled | 409ms | |
| `<exception>` | ✅ Compiled | 432ms | |
| `<limits>` | ✅ Compiled | 1269ms | |
| `<new>` | ✅ Compiled | 47ms | |
| `<pair_swap_deleted_member>` | ✅ Compiled | 23ms | |
| `<rel_ops_no_false_instantiation_ret0>` | ✅ Compiled | 660ms | |
| `<source_location>` | ✅ Compiled | 33ms | |
| `<span>` | ✅ Compiled | 35ms | |
| `<type_traits>` | ✅ Compiled | 351ms | **Unblocked from `std::is_pointer<int*>::value` static assert failure.** |
| `<type_traits_is_integral_any_of_fail>` | ✅ Compiled | 16ms | |
| `<typeinfo_ret0>` | ✅ Compiled | 47ms | |
| `<utility>` | ✅ Compiled | 657ms | |
| `<version>` | ✅ Compiled | 34ms | |
| `<algorithm>` | ❌ Compile Error | 2000ms | `Cannot use copy initialization with explicit constructor`. |
| `<any>` | ❌ Compile Error | 477ms | No first-order diagnostic captured by the sweep grep. |
| `<array>` | ❌ Codegen Error | see focused note | Injected-class-name fix unblocks the `std::reverse_iterator` explicit-ctor false positive; now progresses into existing IR/codegen gaps around unresolved constructor/placeholder lowering. |
| `<atomic>` | ❌ Compile Error | 749ms | `Fatal error: Base class instantiation name should resolve after default filling`. |
| `<chrono>` | ❌ Compile Error | 2942ms | `Fatal error: Unregistered dependent placeholder type reached template argument classification`. |
| `<cmath>` | ❌ Compile Error | 4922ms | Non-dependent name `__poly_hermite_recursion` C++20 [temp.res]/9 violation. |
| `<deque>` | ❌ Compile Error | 1912ms | Non-dependent name `_M_deallocate_node` C++20 [temp.res]/9 violation. |
| `<fstream>` | ❌ Compile Error | 6583ms | Non-dependent name `__cerb` C++20 [temp.res]/9 violation. |
| `<functional>` | ❌ Compile Error | 1901ms | `ExpressionSubstitutor missing binding for ordered template parameter '_Head'`. |
| `<iostream>` | ❌ Compile Error | 6550ms | Non-dependent name `__cerb` C++20 [temp.res]/9 violation. |
| `<iterator>` | ❌ Compile Error | 2272ms | Call to deleted function `swap`. |
| `<latch>` | ❌ Compile Error | 820ms | `Fatal error: Base class instantiation name should resolve after default filling`. |
| `<list>` | ❌ Compile Error | 2136ms | `ExpressionSubstitutor missing binding for ordered template parameter '_Head'`. |
| `<map>` | ❌ Compile Error | 1993ms | `Fatal error: Unregistered dependent placeholder type reached template argument classification`. |
| `<memory>` | ❌ Compile Error | 2417ms | `ExpressionSubstitutor missing binding for ordered template parameter '_Head'`. |
| `<numeric>` | ❌ Compile Error | 2023ms | `Itanium name mangling: unresolved 'auto' type reached mangling` for `__begin`. |
| `<optional>` | ❌ Compile Error | 997ms | Member `_M_engaged` not found in `_Optional_payload`. |
| `<optional_codegen_recovery>` | ❌ Compile Error | 999ms | Member `_M_engaged` not found in `_Optional_payload`. |
| `<queue>` | ❌ Compile Error | 1974ms | Non-dependent name `_M_deallocate_node` C++20 [temp.res]/9 violation. |
| `<ranges>` | ❌ Compile Error | 2414ms | Call to deleted function `swap`. |
| `<ratio>` | ❌ Compile Error | 486ms | `std::ratio_less` static assert is still not a constant expression. |
| `<set>` | ❌ Compile Error | 1966ms | `Fatal error: Unregistered dependent placeholder type reached template argument classification`. |
| `<shared_mutex>` | ❌ Compile Error | 1989ms | `Fatal error: Unregistered dependent placeholder type reached template argument classification`. |
| `<sstream>` | ❌ Compile Error | 6579ms | Non-dependent name `__cerb` C++20 [temp.res]/9 violation. |
| `<stack>` | ❌ Compile Error | 1933ms | Non-dependent name `_M_deallocate_node` C++20 [temp.res]/9 violation. |
| `<stdexcept>` | ❌ Compile Error | 6501ms | `Ambiguous constructor call`. |
| `<string>` | ❌ Compile Error | 6448ms | `Ambiguous constructor call`. |
| `<string_view>` | ❌ Compile Error | 2092ms | `cannot initialize a variable of type 'struct' with an lvalue of type 'const char[14]'`. |
| `<tuple>` | ❌ Compile Error | 1502ms | `ExpressionSubstitutor missing binding for ordered template parameter '_Head'`. |
| `<variant>` | ❌ Compile Error | 2259ms | Static assert failed during template instantiation after a segmentation-fault diagnostic line. |
| `<vector>` | ❌ Compile Error | 1620ms | `Itanium name mangling: unknown type — cannot generate valid symbol`. |
| `<wstring_view_find_ret0>` | ❌ Compile Error | 2025ms | No matching member function for call to `find`. |

Summary: 16 std-header tests compile cleanly in this sweep.  `<type_traits>` is
newly clean because `std::is_pointer<int*>::value` now selects the pointer
partial specialization through `__remove_cv_t<int*>`.  The next broad blockers
remain explicit-constructor copy-initialization, non-dependent-name false
positives, tuple `_Head` substitution, unresolved dependent placeholders, and
`__begin`/unknown-type mangling.


#### 2026-04-30 Static-member NTTP fast-path fix in partial specializations (Linux/libstdc++-14)

This pass retested every `tests/std/test_std_*.cpp` after rebuilding
`x64/Sharded/FlashCpp` with clang++ and includes one targeted parser fix.

Fix landed:

- **`classifySimpleTemplateArgName` now also probes the surrounding struct /
  member-function parsing contexts (resolving the `StructTypeInfo` from
  `MemberFunctionContext::struct_type_index` when the cached
  `local_struct_info` is null).**  Static data members of an *eagerly
  reparsed* partial-specialization member function body — e.g.
  `__atomic_ref<_Tp,false,false>::is_lock_free()`'s
  `is_lock_free<sizeof(_Tp), required_alignment>()` in
  `<bits/atomic_base.h>` line 1523 — are now classified as **ValueLike**
  NTTP arguments instead of **Unknown**, so the parser no longer
  mis-interprets them as types and aborts with `Missing semicolon(;)`.

Regression: `tests/test_partial_spec_static_member_nttp_ret0.cpp`.

Re-run snapshot of `tests/std/test_std_*.cpp` (Linux/libstdc++-14, clang++
build, 60 s timeout per file).  Many headers regressed from earlier dated
sections because of unrelated *Missing TypeInfo while computing template
argument size* and *Unregistered dependent placeholder type* internal
errors that are still triaged separately:

| Header | Status | Time | First-order stop / note |
|--------|--------|------|-------------------------|
| `<aggregate_brace_elision_follow>` | ✅ PASS | 44ms | |
| `<algorithm>` | ❌ Compile Error | 2450ms | `Cannot use copy initialization with explicit constructor` (`std::reverse_iterator` variable initialization). |
| `<any>` | ❌ Compile Error | 603ms | Earlier alias-template registration log; first hard error is later. |
| `<array>` | ❌ Compile Error | 1459ms | `Cannot use copy initialization with explicit constructor`. |
| `<atomic>` | ❌ Compile Error | 936ms | Old `Missing semicolon(;)` parse stop **fixed**.  Now stops further in at `Fatal error: Base class instantiation name should resolve after default filling` (same blocker as `<latch>` — `__atomic_impl::__compare_exchange<_AtomicRef>(...)` is mis-classified as a variable template). |
| `<bit>` | ✅ PASS | 597ms | |
| `<chrono>` | ❌ Compile Error | 3726ms | `Unregistered dependent placeholder type reached template argument classification`. |
| `<cmath>` | ❌ Compile Error | 4254ms | `Missing TypeInfo while computing template argument size`. |
| `<compare>` | ✅ PASS | 34ms | |
| `<concepts>` | ✅ PASS | 509ms | |
| `<deque>` | ❌ Compile Error | 1734ms | `Missing TypeInfo while computing template argument size`. |
| `<exception>` | ✅ PASS | 546ms | |
| `<fstream>` | ❌ Compile Error | 3150ms | `Missing TypeInfo while computing template argument size`. |
| `<functional>` | ❌ Compile Error | 2455ms | `ExpressionSubstitutor missing binding for ordered template parameter '_Head'`. |
| `<iostream>` | ❌ Compile Error | 3144ms | `Missing TypeInfo while computing template argument size`. |
| `<iterator>` | ❌ Compile Error | 2910ms | `stl_pair.h:308: Call to deleted function 'swap'`. |
| `<latch>` | ❌ Compile Error | 932ms | Same `Base class instantiation name should resolve after default filling` blocker as `<atomic>` after the static-member NTTP fix. |
| `<limits>` | ✅ PASS | 1501ms | |
| `<list>` | ❌ Compile Error | 1754ms | `Missing TypeInfo while computing template argument size`. |
| `<map>` | ❌ Compile Error | 2498ms | `Unregistered dependent placeholder type reached template argument classification`. |
| `<memory>` | ❌ Compile Error | 3067ms | `ExpressionSubstitutor missing binding for ordered template parameter '_Head'`. |
| `<new>` | ✅ PASS | 62ms | |
| `<numeric>` | ❌ Compile Error | 2448ms | First-order error not surfaced as `error:`/`Fatal:`. |
| `<optional>` | ❌ Compile Error | 1255ms | `struct type info not found`. |
| `<optional_codegen_recovery>` | ❌ Compile Error | 1288ms | `struct type info not found`. |
| `<pair_swap_deleted_member>` | ✅ PASS | 28ms | |
| `<queue>` | ❌ Compile Error | 1820ms | `Missing TypeInfo while computing template argument size`. |
| `<ranges>` | ❌ Compile Error | 3084ms | `stl_pair.h:308: Call to deleted function 'swap'`. |
| `<ratio>` | ❌ Compile Error | 604ms | `static_assert condition is not a constant expression: Undefined qualified identifier in constant expression: ratio_less$...::value`. |
| `<rel_ops_no_false_instantiation_ret0>` | ✅ PASS | 833ms | |
| `<set>` | ❌ Compile Error | 2481ms | `Missing TypeInfo while computing template argument size`. |
| `<shared_mutex>` | ❌ Compile Error | 2400ms | `Unregistered dependent placeholder type reached template argument classification`. |
| `<source_location>` | ✅ PASS | 46ms | |
| `<span>` | ✅ PASS | 47ms | |
| `<sstream>` | ❌ Compile Error | 3144ms | `Missing TypeInfo while computing template argument size`. |
| `<stack>` | ❌ Compile Error | 1758ms | `Missing TypeInfo while computing template argument size`. |
| `<stdexcept>` | ❌ Compile Error | 2933ms | `Missing TypeInfo while computing template argument size`. |
| `<string>` | ❌ Compile Error | 2838ms | `Missing TypeInfo while computing template argument size`. |
| `<string_view>` | ❌ Compile Error | 2583ms | `Cannot use copy initialization with explicit constructor` (`std::ranges::__detail::__max_size_type` return statement). |
| `<tuple>` | ❌ Compile Error | 1875ms | `ExpressionSubstitutor missing binding for ordered template parameter '_Head'`. |
| `<type_traits>` | ❌ Compile Error | 418ms | `static_assert failed`. |
| `<type_traits_is_integral_any_of_fail>` | ✅ PASS | 20ms | |
| `<typeinfo_ret0>` | ✅ PASS | 63ms | |
| `<utility>` | ✅ PASS | 830ms | |
| `<variant>` | 💥 Crash | 2801ms | Still SIGSEGVs deep in `std::__invoke` overload resolution. |
| `<vector>` | ❌ Compile Error | 2062ms | `Itanium name mangling: unknown type — cannot generate valid symbol`. |
| `<version>` | ✅ PASS | 45ms | |
| `<wstring_view_find_ret0>` | ❌ Compile Error | 2504ms | `No matching member function for call to 'find'`. |

The latest retest no longer reproduces the old `Missing TypeInfo while
computing template argument size` error in the headers rechecked in this pass;
those files now reach later semantic-analysis / template-substitution blockers
instead. The most visible current next-stop blockers in this slice are:

1. **Explicit-constructor copy-init misclassification** —
   `<string_view>` and `<algorithm>` now fail later with
   `Cannot use copy initialization with explicit constructor`.
2. **Deep template binding / placeholder recovery** —
   `<memory>` now stops at
   `ExpressionSubstitutor missing binding for ordered template parameter '_Head'`,
   `<map>` stops at
   `Unregistered dependent placeholder type reached template argument classification`,
   and `<vector>` now reaches
   `Itanium name mangling: unknown type — cannot generate valid symbol`.


#### 2026-05-04 Shift-result sema type fix for `<limits>` (Linux/libstdc++-14)

This pass rebuilt `x64/Sharded/FlashCpp` with clang++ and rechecked the
current `<limits>` blocker plus focused binary-conversion regressions.

Fix landed:

- **`inferExpressionType` now models C++20 [expr.shift] result typing instead
  of falling through to the usual arithmetic conversions.**  Shift operands
  undergo independent integral promotions, and the result has the promoted
  left-operand type.  The old common-type fallback made expressions shaped like
  libstdc++'s `__glibcxx_max_b(char32_t, ...)` infer an intermediate
  `unsigned long long` result for `((char32_t)1 << n)`, so sema annotated the
  following `- 1` as `int -> unsigned long long` while codegen correctly
  expected `int -> char32_t`.
- Binary operand conversion annotation now runs after child expression
  normalization, so parent binary operators consume the sema-owned types of
  casts and nested shifts before selecting the usual arithmetic conversions.

Regression test:

- `tests/test_char_width_binary_sema_conversions_ret0.cpp`

Validation snapshot:

| Header/Test | Status | Time | First-order stop / note |
|-------------|--------|------|-------------------------|
| `tests/test_char_width_binary_sema_conversions_ret0.cpp` | ✅ PASS | run-all focused slice | Covers `wchar_t < int`, `char32_t - int`, and ordinary mixed builtin arithmetic. |
| `<limits>` / `test_std_limits.cpp` | ✅ Compiled | ~1418ms | Wide-character extrema codegen now compiles without the old oversized immediate-return skips. |

#### 2026-05-04 Same-template / unknown-struct copy-init explicit-ctor fix (Linux/libstdc++-14)

This pass rebuilt `x64/Sharded/FlashCpp` with clang++ on Linux and ran the full
`tests/std/test_std_*.cpp` set against libstdc++-14.

Fix landed:

- **`tryAnnotateCopyInitConvertingConstructor` no longer flags two false-positive
  shapes that caused `Cannot use copy initialization with explicit constructor`
  to fire across `<array>`, `<algorithm>`, `<numeric>`, `<optional>`,
  `<optional_codegen_recovery>`, `<stdexcept>`, `<string>`, and `<string_view>`:**
    1. Inside a deferred class-template member body, an unqualified reference
       to the class name resolves to the *pattern* `type_index`, while
       expressions of the same template (for example `*this` or another local
       of the same kind) carry an *instantiated* `type_index`.  The exact-match
       early return therefore missed and the converting-ctor scan picked an
       `explicit` ctor (e.g. `reverse_iterator(iterator_type)`) as the only
       viable candidate.  The function now treats `from`/`to` pairs whose
       struct base names match (after stripping the trailing namespace
       component and any `$pattern_…`/`$hash` suffix) as same-type copy/move
       initialization and bails out before scanning converting ctors.
    2. When the source canonical desc reports `category() == Struct` but its
       `type_index` is the unresolved `0` placeholder, the function used to
       still find an explicit converting ctor on the target and emit the
       diagnostic against an unknown source.  It now bails out instead of
       guessing.
- **`inferExpressionType` for unary `+` / `-` / `~` no longer synthesises a
  generic `nativeTypeIndex(Struct)` for non-arithmetic operands.** Pointers,
  arrays, structs, and other non-arithmetic categories now return `{}` from the
  fast path so callers fall back to other inference routes (or skip
  conversion-driven diagnostics) instead of comparing against a placeholder
  type id that no real struct produces.  Same fix also restores `pointer` /
  `array` short-circuits that previously fell through.

Regression tests:

- `tests/test_explicit_ctor_same_template_copy_init_ret0.cpp`
- `tests/test_unary_struct_operator_copy_init_ret0.cpp`

Full Linux regression suite (`bash tests/run_all_tests.sh`): 2265 pass / 0 fail
/ 154 `_fail` correct.

Linux/libstdc++-14 std-header sweep (`x64/Sharded/FlashCpp`, 60 s timeout):

| Header | Status | Time | First-order stop / note |
|--------|--------|------|-------------------------|
| `<aggregate_brace_elision_follow>` | ✅ Compiled | 44ms | |
| `<bit>` | ✅ Compiled | 615ms | |
| `<compare_ret42>` | ✅ Compiled | 34ms | |
| `<concepts>` | ✅ Compiled | 517ms | |
| `<exception>` | ✅ Compiled | 553ms | |
| `<limits>` | ✅ Compiled | 1418ms | **Unblocked from Phase 15 missed binary conversions.** Wide-character extrema now compile without the old oversized immediate-return skips. |
| `<new>` | ✅ Compiled | 63ms | |
| `<pair_swap_deleted_member>` | ✅ Compiled | 27ms | |
| `<rel_ops_no_false_instantiation_ret0>` | ✅ Compiled | 826ms | |
| `<source_location>` | ✅ Compiled | 44ms | |
| `<span>` | ✅ Compiled | 47ms | |
| `<type_traits_is_integral_any_of_fail>` | ✅ Compiled | 19ms | |
| `<typeinfo_ret0>` | ✅ Compiled | 62ms | |
| `<utility>` | ✅ Compiled | 814ms | |
| `<version>` | ✅ Compiled | 44ms | |
| `<algorithm>` | ❌ Compile Error | 2758ms | **Unblocked from explicit-ctor copy-init.** Now reaches `Itanium name mangling: unresolved 'auto' type reached mangling` for `__begin`. |
| `<any>` | ❌ Compile Error | 602ms | `Ambiguous constructor call`. |
| `<array>` | ❌ Compile Error | 1537ms | **Unblocked from explicit-ctor copy-init.** Now reaches `Itanium name mangling: unresolved 'auto' type reached mangling` for `__begin`. |
| `<atomic>` | ❌ Compile Error | 954ms | `Fatal error: Base class instantiation name should resolve after default filling`. |
| `<chrono>` | ❌ Compile Error | 3747ms | `Fatal error: Unregistered dependent placeholder type reached template argument classification`. |
| `<cmath>` | ❌ Compile Error | 5823ms | Non-dependent name `__poly_hermite_recursion` C++20 [temp.res]/9 violation. |
| `<deque>` | ❌ Compile Error | 2433ms | Non-dependent name `_M_deallocate_node` C++20 [temp.res]/9 violation. |
| `<fstream>` | ❌ Compile Error | 8368ms | Non-dependent name `__cerb` C++20 [temp.res]/9 violation. |
| `<functional>` | ❌ Compile Error | 2412ms | `ExpressionSubstitutor missing binding for ordered template parameter '_Head'`. |
| `<iostream>` | ❌ Compile Error | 8121ms | Non-dependent name `__cerb` C++20 [temp.res]/9 violation. |
| `<iterator>` | ❌ Compile Error | 2896ms | (No first-order parser/sema diagnostic captured by the sweep grep — drops out of codegen with rc=1.) |
| `<latch>` | ❌ Compile Error | 959ms | `Fatal error: Base class instantiation name should resolve after default filling`. |
| `<list>` | ❌ Compile Error | 2660ms | `ExpressionSubstitutor missing binding for ordered template parameter '_Head'`. |
| `<map>` | ❌ Compile Error | 2451ms | `Fatal error: Unregistered dependent placeholder type reached template argument classification`. |
| `<memory>` | ❌ Compile Error | 3136ms | `ExpressionSubstitutor missing binding for ordered template parameter '_Head'`. |
| `<numeric>` | ❌ Compile Error | 2411ms | **Unblocked from explicit-ctor copy-init.** Now reaches `Itanium name mangling: unresolved 'auto' type reached mangling` for `__begin`. |
| `<optional>` | ❌ Compile Error | 1248ms | **Unblocked from explicit-ctor copy-init.** Now reaches `Itanium name mangling: unresolved 'auto' type reached mangling` for `__begin`. |
| `<optional_codegen_recovery>` | ❌ Compile Error | 1258ms | **Unblocked from explicit-ctor copy-init.** Now reaches `Itanium name mangling: unresolved 'auto' type reached mangling` for `__begin`. |
| `<queue>` | ❌ Compile Error | 2528ms | Non-dependent name `_M_deallocate_node` C++20 [temp.res]/9 violation. |
| `<ranges>` | ❌ Compile Error | 3064ms | (No first-order diagnostic captured by sweep grep.) |
| `<ratio>` | ❌ Compile Error | 605ms | `static_assert` involving `std::ratio_less` still evaluates as `Undefined qualified identifier in constant expression`. |
| `<set>` | ❌ Compile Error | 2443ms | `Fatal error: Unregistered dependent placeholder type reached template argument classification`. |
| `<shared_mutex>` | ❌ Compile Error | 2353ms | `Fatal error: Unregistered dependent placeholder type reached template argument classification`. |
| `<sstream>` | ❌ Compile Error | 7955ms | Non-dependent name `__cerb` C++20 [temp.res]/9 violation. |
| `<stack>` | ❌ Compile Error | 2423ms | Non-dependent name `_M_deallocate_node` C++20 [temp.res]/9 violation. |
| `<stdexcept>` | ❌ Compile Error | 9408ms | **Unblocked from explicit-ctor copy-init.** Now reaches `Itanium name mangling: unresolved 'auto' type reached mangling` for `__begin`. |
| `<string>` | ❌ Compile Error | 8484ms | **Unblocked from explicit-ctor copy-init.** Now reaches `cannot initialize a variable of type 'struct' with an lvalue of type 'const char[…]'`. |
| `<string_view>` | ❌ Compile Error | 2585ms | **Unblocked from explicit-ctor copy-init.** Now reaches `cannot initialize a variable of type 'struct' with an lvalue of type 'const char[…]'`. |
| `<tuple>` | ❌ Compile Error | 1870ms | `ExpressionSubstitutor missing binding for ordered template parameter '_Head'`. |
| `<type_traits>` | ❌ Compile Error | 412ms | `static_assert(std::is_integral<int>::value)` still fails. |
| `<variant>` | 💥 Crash | 2734ms | Segmentation fault deeper in visitation/`std::__invoke` instantiation. |
| `<vector>` | ❌ Compile Error | 2013ms | `Itanium name mangling: unknown type — cannot generate valid symbol`. |
| `<wstring_view_find_ret0>` | ❌ Compile Error | 2506ms | (No first-order diagnostic captured by sweep grep.) |

Summary: 15 compile cleanly (up from 14 before this fix; `<utility>` and the
focused `<rel_ops>`/`<typeinfo>`/`<aggregate_brace_elision_follow>` regressions
were already passing).  The eight std-header tests that previously failed on
the explicit-ctor copy-init diagnostic now reach the next layer of compiler
issues (mostly `__begin` `auto`-mangling and `const char[N]` aggregate
initialization), which are tracked separately as new high-value blockers.

Newly observed top blockers, ordered by header impact:

1. **Itanium name mangling: unresolved `auto` type reached mangling** for
   `__begin` (libstdc++ ranges CPO).  Affects: `<algorithm>`, `<array>`,
   `<numeric>`, `<optional>`, `<optional_codegen_recovery>`, `<stdexcept>`.
2. **Non-dependent name C++20 [temp.res]/9 violation** for symbols like
   `_M_deallocate_node`, `__cerb`, `__poly_hermite_recursion` — this is the
   same false-positive class flagged earlier in `<functional>`'s
   `__node_gen` and is likely the depth-guard or instantiation-context fallout
   referenced under "Recent Fixes (2026-04-24)".  Affects: `<deque>`,
   `<queue>`, `<stack>`, `<iostream>`, `<sstream>`, `<fstream>`, `<cmath>`.
3. **`ExpressionSubstitutor missing binding for ordered template parameter
   '_Head'`** — shared by all `<tuple>`-derived containers.  Affects:
   `<tuple>`, `<list>`, `<memory>`, `<functional>`.
4. **`Unregistered dependent placeholder type reached template argument
   classification`** — affects `<chrono>`, `<map>`, `<set>`, `<shared_mutex>`.
5. **`Base class instantiation name should resolve after default filling`** —
   affects `<atomic>`, `<latch>`.



This pass rebuilt `x64/Sharded/FlashCpp` with clang++ and retested
`<string_view>`, `<algorithm>`, `<vector>`, `<memory>`, and `<map>`.

Fixes landed:

- **Template type-argument reification no longer hard-fails on
  missing/incomplete struct `TypeInfo`.** `computeTemplateTypeArgSizeBits`
  now resolves aliases, keeps known sizes when available, and otherwise returns
  `0` so later sema can diagnose only contexts that really require a complete
  object type.
- **Copy-initialization from a same-type constructor-call prvalue no longer
  incorrectly rejects explicit constructors in the focused regression case.**
  This covers patterns like `T x = T(args);` and `return T(args);`.

Regression tests:

- `tests/test_copy_init_same_type_ctor_ret0.cpp`


#### 2026-04-30 Dependent identifier NTTP template-argument fix (Linux/libstdc++-14)

This pass rebuilt `x64/Sharded/FlashCpp` with clang++ and retested the
current `<atomic>`, `<latch>`, and `<variant>` blockers after a parser-only
fix for dependent identifier non-type template arguments.

Fixes landed:

- **`parse_explicit_template_arguments` now accepts bare dependent identifier
  NTTP arguments without reparsing them as types.**  This covers cases such as
  `required_alignment` and `__j` when constant evaluation is intentionally
  deferred inside a template body.
- **Expression parsing no longer treats every `lookupTemplate()` hit as a class
  template.**  In expression context, only names explicitly registered as class
  templates take the type-like path, so function-template ids such as
  `is_lock_free<...>()` stay expression-like.

Regression tests:

- `tests/test_function_template_dependent_identifier_nttp_ret0.cpp`
- `tests/test_variable_template_dependent_identifier_nttp_ret0.cpp`

Focused std-header impact (current first-order stops, Linux/libstdc++-14):

| Header | Status | Time | First-order stop / note |
|--------|--------|------|-------------------------|
| `<atomic>` | ❌ Compile Error | 1060ms | The old `required_alignment` parse stop is gone. It now instantiates deeper and first stops at `Variable template '__compare_exchange' not found`, then throws `Fatal error: Base class instantiation name should resolve after default filling`. |
| `<latch>` | ❌ Compile Error | 1050ms | Same blocker as `<atomic>` through `<bits/atomic_base.h>`; the previous `is_lock_free<sizeof(_Tp), required_alignment>()` parse regression is fixed. |
| `<variant>` | 💥 Crash | 2910ms | The `in_place_index<__j>` parse stop is gone. The test now reaches deeper visitation / `std::__invoke` instantiation before crashing with signal 11. |


#### 2026-04-30 Pointer-arithmetic ternary result type fix (Linux/libstdc++-14)

This pass retested every `tests/std/test_std_*.cpp` after rebuilding
`x64/Sharded/FlashCpp` with clang++.  It also includes one targeted fix.

Fix landed:

- **Sema now models pointer arithmetic in `inferExpressionType`** per
  C++20 [expr.add]:
  - `T* + integral` and `integral + T*` -> `T*`
  - `T* - integral` -> `T*`
  - `T* - T*` -> `ptrdiff_t`

  Previously, any binary operator with a pointer operand returned
  `nullopt` from sema's expression type inference, so a sema-normalized
  ternary whose branches involved pointer arithmetic could not compute
  a sema-owned result type.  After the recent commit that made codegen
  *require* an exact ternary result type for normalized function bodies,
  this regressed `<typeinfo>`'s `name()` member, whose body is exactly
  `return __name[0] == '*' ? __name + 1 : __name;`, with codegen
  throwing `Sema-normalized ternary expression missing exact result
  type`.

Regression: `tests/test_ternary_pointer_arithmetic_branches_ret0.cpp`.

Focused std-header impact (current first-order stops, Linux/libstdc++-14):

| Header | Status | Time | First-order stop / note |
|--------|--------|------|-------------------------|
| `<typeinfo>` | ✅ Compiled | 46ms | Now passes via the pointer-arithmetic ternary fix above. |
| `<limits>` | ✅ Compiled | 1222ms | |
| `<utility>` | ✅ Compiled | 639ms | |
| `<concepts>` | ✅ Compiled | 392ms | |
| `<exception>` | ✅ Compiled | 418ms | |
| `<bit>` | ✅ Compiled | 464ms | |
| `<new>` | ✅ Compiled | 46ms | |
| `<span>` | ✅ Compiled | 34ms | |
| `<source_location>` | ✅ Compiled | 33ms | |
| `<version>` | ✅ Compiled | 34ms | |
| `<compare>` | ✅ Compiled (focused) | 26ms | `test_std_compare_ret42.cpp` only. |
| `<type_traits>` | ❌ Compile Error | 318ms | Still `static_assert(std::is_pointer<int*>::value)`. |
| `<atomic>` | ❌ Parse Error | 706ms | Still stops at `__atomic_impl::is_lock_free<sizeof(_Tp), required_alignment>();`.  Investigation: deferred template-body parsing accepts `sizeof(_Tp)` as a dependent compile-time NTTP arg, but a bare identifier NTTP arg referring to a class-template static constexpr member (here `required_alignment`) is reparsed as a *type* in the second slot, which then forces the parser to abandon the call as comparison.  Fix would extend `parse_explicit_template_arguments` to accept dependent identifier NTTP args (similar to the existing `is_compile_time_expr` path) when the expression branch's constant evaluation cannot succeed because we're parsing a deferred template body. |
| `<variant>` | ❌ Parse Error | 841ms | Still stops at libstdc++ `variant:597` while parsing `in_place_index<__j>`. |
| `<ratio>` | ❌ Compile Error | 471ms | |
| `<latch>` | ❌ Parse Error | 704ms | (Regressed since the 2026-04-24 sweep — needs a re-investigation.) |
| `<any>` | ❌ Compile Error | 461ms | |
| `<optional>` | ❌ Compile Error | 971ms | |
| `<array>` | ❌ Compile Error | 1131ms | |
| `<vector>` / `<deque>` / `<list>` / `<stack>` / `<queue>` | ❌ Compile Error | 1.1–1.4s | |
| `<set>` / `<map>` | ❌ Compile Error | 1.9s | |
| `<tuple>` | ❌ Compile Error | 1591ms | |
| `<string>` / `<string_view>` / `<wstring_view_find_ret0>` | ❌ Compile Error | ~2.0s | |
| `<algorithm>` / `<numeric>` / `<functional>` / `<iterator>` / `<ranges>` | ❌ Compile Error | 1.9–2.4s | |
| `<chrono>` | ❌ Compile Error | 2880ms | |
| `<cmath>` | ❌ Compile Error | 3324ms | |
| `<memory>` | ❌ Compile Error | 1788ms | |
| `<shared_mutex>` | ❌ Compile Error | 1915ms | |
| `<stdexcept>` | ❌ Compile Error | 2257ms | |
| `<iostream>` / `<sstream>` / `<fstream>` | ❌ Compile Error | ~2.4s | (No longer crashing — now reports compile errors.) |

Notes:

- Previously crashing tests `<deque>`, `<queue>`, `<stack>`, `<iostream>`,
  `<sstream>`, `<fstream>`, `<stdexcept>`, `<barrier>` are now reaching
  compile errors instead of hard crashes (rc=1, signal 0).
- Prior tests reported as ✅ in the 2026-04-24 sweep — `<latch>`,
  `<atomic>`, `<exception>`, `<variant>` — were retested individually:
  `<exception>` still passes; `<atomic>`, `<variant>`, `<latch>` regressed
  to first-order parse / compile errors and need re-investigation
  (likely candidates: the recent template-parameter / deferred
  template-body changes).



This pass retested a small set of standard-header probes against
`x64/Sharded/FlashCpp` after rebuilding with clang++.

Fixes landed:

- **Non-dependent alias template substitutions now preserve substituted type
  pointer/cv/ref metadata before partial-specialization matching.**  Direct
  aliases such as `identity_t<int*>` and `add_pointer_t<int>` now keep the
  pointer levels required for `T*` partial specializations instead of reducing
  the argument to the base `TypeIndex` only.

Regression:

- `tests/test_alias_template_pointer_modifiers_ret0.cpp`

Focused std-header impact:

- `<type_traits>` (`test_std_type_traits.cpp`) still fails at
  `static_assert(std::is_pointer<int*>::value)` in ~330ms.  The remaining
  blocker is the dependent/member-alias form used by libstdc++:
  `is_pointer<T>` derives from
  `__is_pointer_helper<__remove_cv_t<T>>::type`, and concrete metadata from
  `typename remove_cv<T>::type` is still not preserved through the deferred
  base-class argument path.
- `<array>` (`test_std_array.cpp`) still reaches semantic/codegen and fails on
  copy-initialization of explicit `std::reverse_iterator` in ~1240ms.
- `<atomic>` (`test_std_atomic.cpp`) currently stops parsing
  `__atomic_impl::is_lock_free<sizeof(_Tp), required_alignment>()` in ~810ms.
- `<variant>` (`test_std_variant.cpp`) currently stops at libstdc++
  `variant:597` while parsing `in_place_index<__j>` in ~930ms.
- `<utility>` (`test_std_utility.cpp`) still compiles successfully.

#### 2026-04-28 Dependent `decltype` / Member Alias Deferral Sweep (Linux/libstdc++-14)

This sweep targeted the current first-order stop shared by many standard
headers: `Fatal error: Instantiated member alias target should resolve before
alias copy` after registering libstdc++'s
`__do_common_type_impl::__cond_t`.

Fixes landed:

- **Dependent `decltype(expr)` in active template contexts now materializes as
  an explicit incomplete dependent placeholder, not as concrete `auto`**
  (`src/Parser_TypeSpecifiers.cpp`).  This keeps
  `decltype(true ? declval<T>() : declval<U>())` dependent per C++20 template
  dependency rules instead of instantiating downstream aliases such as
  `decay<auto>`.
- **Instantiated member-alias target probing now defers when the concrete
  `Base::member` alias is not materialized yet** rather than throwing an
  `InternalError` (`src/Parser_Templates_Inst_Substitution.cpp`).  The caller's
  normal alias-copy path keeps the unresolved dependent target visible for later
  substitution.
- **Deferred alias-target capture now restores the already-consumed token
  position if the target is not a template-id** (`src/Parser_Templates_Class.cpp`).
  This preserves valid non-template-id alias targets such as
  `decltype((typename __promote<T>::__type(0) + ...))`.

Regression:

- `tests/test_dependent_decltype_alias_template_ret0.cpp`
- Existing `tests/test_typename_funccast_fold_ret0.cpp` was re-run because the
  same `decltype`/fold-expression alias shape appears in libstdc++'s
  `ext/type_traits.h`.

Focused std-header impact:

- **Newly clean**: `<utility>` (`test_std_utility.cpp`) ~587ms.  This test now
  compiles fully instead of stopping in `__do_common_type_impl::__cond_t`.
- **Still clean in this sweep**: `<aggregate_brace_elision_follow>` ~35ms,
  `<bit>` ~412ms, `<compare_ret42>` ~25ms, `<concepts>` ~340ms,
  `<exception>` ~370ms, `<limits>` ~1268ms, `<new>` ~45ms,
  `<pair_swap_deleted_member>` ~21ms, `<rel_ops_no_false_instantiation_ret0>`
  ~586ms, `<source_location>` ~34ms, `<span>` ~34ms, `<typeinfo_ret0>` ~46ms,
  and `<version>` ~33ms.
- **Remaining first-order blockers after this fix**:
  - `<algorithm>` ~1683ms and `<array>` ~1018ms — copy-initialization is still
    used for explicit `std::reverse_iterator` construction.
  - `<string>`, `<string_view>`, and `<stdexcept>` — copy-initialization is still
    used for explicit `std::ranges::__detail::__max_size_type` construction.
  - `<atomic>` ~662ms and `<latch>` ~658ms — parser stops at
    `__atomic_impl::is_lock_free<sizeof(_Tp), required_alignment>()`.
  - `<tuple>` ~1536ms and `<list>` ~2051ms — unsupported
    `PackExpansionExprNode` instances still reach the sema-owned AST surface.
  - `<deque>` ~1624ms, `<queue>` ~1737ms, and `<stack>` ~1712ms —
    depth-guard-related non-dependent-name false positive on
    `_M_deallocate_node`.
  - `<fstream>` ~6088ms, `<iostream>` ~6072ms, and `<sstream>` ~6496ms —
    non-dependent-name false positive on `__cerb`.
  - `<functional>` ~2337ms, `<map>` ~1718ms, `<set>` ~1722ms,
    `<shared_mutex>` ~1926ms, and `<chrono>` ~2871ms — current first stop is
    `Unregistered dependent placeholder type reached template argument classification`.
  - `<ratio>` ~421ms — no longer hits the member-alias `InternalError`; now
    returns to the known `ratio_less<...>::value` constant-evaluation gap.
  - `<type_traits>` ~282ms — `std::is_integral<int>::value` still passes, but
    `std::is_pointer<int*>::value` currently fails.
  - `<variant>` ~777ms — no longer SIGSEGVs in this sweep; current first stop is
    `variant:597` "Expected primary expression".

#### 2026-04-27 std::pair constructor-template retest (Linux/libstdc++-14)

Focused retest against `x64/Sharded/FlashCpp` after rebuilding with clang++:

- `<utility>` (`test_std_utility.cpp`) still fails, but now only with the
  `std::rel_ops` semantic errors (`operator<=`, `operator>`, `operator>=`
  requiring `operator<`). The earlier `std::pair` constructor-template codegen
  crash is no longer present in the retest log.
- Added reduced regression
  `tests/test_class_template_unresolved_ctor_codegen_ret0.cpp`; that narrower
  pair-like reproducer now compiles, links, and returns 0, but it does not
  cover the full libstdc++ `std::pair` overload-resolution failure yet.
- `<compare>` still compiles cleanly in the current Linux/libstdc++-14
  environment (~30ms), so the top table row remains stale for this host.

#### 2026-04-25 Unary Type-Trait Constant-Evaluation Sweep (Linux/libstdc++-14)

This sweep targeted the `<type_traits>` failure where
`std::is_integral<int>::value` evaluated as `false`.  Full regression suite
(`bash tests/run_all_tests.sh`, 2219 tests + 151 `_fail`) still has the
two pre-existing parser failures in `test_template_defaulted_ctor_aggregate_ret0.cpp`
and `test_template_deleted_ctor_aggregate_ret0.cpp`; the new focused regression
passes.

Fixes landed:

- **Exact class-template specialization lookup now retries with alias-normalized
  template arguments before using the instantiation cache**
  (`src/Parser_Templates_Inst_ClassTemplate.cpp`).  This prevents a primary
  template instantiation from hiding a full specialization when the argument
  arrived through an alias-resolved primitive type.
- **Constexpr evaluation of standard unary type-trait `::value` now uses the
  resolved template argument even when inherited `false_type::value` was found
  first** (`src/ConstExprEvaluator_Members.cpp`).  This keeps libstdc++ traits
  such as `std::is_integral<int>` and `std::is_pointer<int*>` semantically
  correct for static assertions.

Regression:

- `tests/test_template_full_specialization_alias_arg_ret0.cpp`

Std-header impact from the 2026-04-25 sweep against `x64/Sharded/FlashCpp`:

- **Newly clean**: `<type_traits>` (`test_std_type_traits.cpp`) ~239ms.
- **Still clean in this sweep**: `<bit>` ~384ms, `<compare>` ~34ms,
  `<concepts>` ~322ms, `<exception>` ~349ms, `<limits>` ~1146ms,
  `<new>` ~50ms, `<source_location>` ~39ms, `<span>` ~39ms,
  `<version>` ~38ms, C compatibility aggregate ~714ms, and the focused
  std regressions listed in `tests/std/`.
- **Representative remaining first-order blockers**: `<array>` now stops at
  unresolved `auto` Itanium mangling (~1092ms); `<algorithm>` at ambiguous
  constructor call (~2046ms); `<string_view>` at `Operator<` (~2637ms);
  `<iterator>` / `<ranges>` at deleted `swap`; `<tuple>` / `<list>` at
  unsupported `PackExpansionExprNode`; `<deque>` / `<queue>` / `<stack>` at
  the depth-guard-triggered `_M_deallocate_node` non-dependent-name check;
  `<variant>` still SIGSEGVs during codegen (~1781ms).

#### 2026-04-24 Member-Function Overload Registration Fix (Linux/libstdc++-14)

This sweep targeted the complete-class member-function lookup path used when
a template's member-initializer list references an unqualified member
function.  Full regression suite (`bash tests/run_all_tests.sh`, 2207 tests
+ 149 `_fail`) = SUCCESS.

Fix landed:

- **Parser complete-class member-function lookup now registers ALL matching
  overloads, not just the first** (`src/Parser_Expr_PrimaryExpr.cpp`).  When
  a template body is being reparsed and an unqualified name like
  `_M_tail(__in)` is looked up inside a member-initializer list, the parser
  previously walked `struct_node->member_functions()` and `break`ed after
  finding the first match by name.  For libstdc++'s `<tuple>`, which
  declares `static _M_tail(_Tuple_impl&)` immediately followed by
  `static _M_tail(const _Tuple_impl&)`, that meant only the non-const
  overload became visible, so the call `_M_tail(__in)` with `__in` of type
  `const _Tuple_impl&` was rejected with "No matching function for call to
  '_M_tail'" at `tuple:399`.  The loop now continues through all overloads
  (same fix applied to the base-class member-function lookup).
  Regression: `tests/test_member_function_overload_in_ctor_init_ret0.cpp`.

Std-header impact: `<tuple>`, `<list>`, `<functional>`, `<memory>` all
advance past `tuple:399` `_M_tail` blocker.  They now stop at the next
layer of issues (unsupported `PackExpansionExprNode` in `<tuple>` / `<list>`,
deeper `string_view:863` template-instantiation failure in `<memory>`,
and a different non-dependent name `__node_gen` in `<functional>`).

Full std-header sweep (Linux/libstdc++-14, `x64/Sharded/FlashCpp`, 45 s
timeout, remeasured against this binary):

- **Compile cleanly (13)**: `<bit>` (~501ms), `<compare>` (~32ms),
  `<concepts>` (~420ms), `<exception>` (~461ms), `<limits>` (~1487ms),
  `<new>` (~59ms), `<pair_swap_deleted_member>` (~26ms),
  `<source_location>` (~44ms), `<span>` (~45ms), `<typeinfo_ret0>` (~60ms),
  `<version>` (~43ms), plus focused regressions. The standalone
  `<initializer_list>` / `<numbers>` / `<typeindex>` / `<new>` / most C
  headers (see table) remain clean.
- **Compile/codegen errors (rc=1, 43 headers)**, grouped by first-order
  root cause (after the member-overload fix):
  - **(5 headers) `<chrono>`, `<fstream>`, `<iostream>`, `<sstream>`,
    `<stdexcept>`, `<string>`** —
    `/usr/include/c++/14/bits/basic_string.h:4699` "Template instantiation
    failed or type not found" in `operator""s` UDL returning
    `basic_string<CharT>` via brace init. Precedes max-depth replay abort
    on `basic_string::find`/`rfind`/`find_first_of`/etc. member templates.
  - **(2 headers) `<tuple>`, `<list>`** — `tuple:25089` / `tuple:38125`
    `unsupported PackExpansionExprNode reached semantic analysis; pack
    expansion should have been eliminated during template substitution`
    — exposed after the member-overload fix unblocked `_M_tail`.
  - **(3 headers) `<deque>`, `<queue>`, `<stack>`** — `stl_deque.h:700`
    non-dependent name `_M_deallocate_node` `[temp.res]/9` check fires
    *after* the template-reparse depth-guard trips (false positive caused
    by the depth-guard aborting mid-reparse, not a real violation).
  - **(1 header) `<functional>`** — `hashtable.h` non-dependent name
    `__node_gen` `[temp.res]/9` (same depth-guard-triggered false positive
    shape as `_M_deallocate_node`).
  - **(1 header) `<memory>`** — `string_view:863` "Template instantiation
    failed or type not found".
  - **(3 headers) `<iterator>`, `<ranges>`, `<map>`, `<set>`** —
    `stl_pair.h:308` / `node_handle.h:285` deleted `swap` overload reached
    during overload resolution (SFINAE'd overload not fully removed).
  - **(3 headers) `<algorithm>`, `<array>`, `<string_view>`** —
    "Cannot use copy initialization with explicit constructor for target
    type `std::reverse_iterator`": `array::rbegin()`'s
    `return reverse_iterator(end())` (a functional-style cast, direct
    init) is being misclassified as copy init in return-statement path.
  - **(1 header) `<cmath>`** — `tr1/poly_hermite.tcc:121` non-dependent
    name `__poly_hermite_recursion` (same depth-guard-triggered false
    positive shape).
  - **(1 header) `<atomic>`** — `atomic:92` `_M_base.load()` - member
    function lookup on `__atomic_base<bool>` misses `load()`.
  - **(1 header) `<vector>`** — "Itanium name mangling: unknown type".
  - **(1 header) `<shared_mutex>`** — "Ambiguous constructor call" for
    `std::chrono::time_point`.
  - **(2 tests) `<optional>`, `<numeric>`,
    `<optional_codegen_recovery>`** — "Itanium: unresolved auto type
    reached mangling".
  - **(1 test) `<ratio>`** — `static_assert` on `std::ratio_less`
    evaluates to "Undefined qualified identifier in constant expression".
  - **(1 test) `<type_traits>`** — `static_assert(std::is_integral<int>::value)`
    fails.
  - **(1 test) `<utility>`** — "Operator< not defined for operand types"
    in generated `operator<=`/`operator>`/`operator>=` for `std::pair`.
  - **(1 test) `<wstring_view_find_ret0>`** — No matching member function
    for call to `find`.
  - **(1 header) `<any>`** — "Ambiguous constructor call" after codegen
    can't find `_Arg` symbol for the templated `any(T&&)` ctor body.
  - **(1 header) `<latch>`** — 10 top-level codegen failures
    (`ConstructorCallOp missing resolved constructor for
    'std::__mutex_base'`, `sema missed function call argument conversion
    (int -> long)`, repeated `__compare_exchange` instantiation loss).
- **Segfaults (2): `<atomic>` post-error, `<variant>` (rc=139)**.
  `<atomic>` still produces a first-order compile error at `atomic:92`
  `_M_base.load()` and then SIGSEGVs during error-path cleanup.
  `<variant>` SIGSEGVs reliably at codegen time without reaching a
  first-order compile error — pre-existing codegen/sema bug in the
  `<variant>` visit machinery.
- **Timeouts: 0** (sticky depth-guards still working).

The `_M_tail` overload bucket (previously 4 headers: `<tuple>`, `<list>`,
`<functional>`, `<memory>`) is gone after this fix. Those headers now
surface first-order errors at deeper layers which are independent bugs.

#### 2026-04-24 Lexer Standards-Compliance + Unary Literal Folding Sweep (Linux/libstdc++-14)

This sweep targeted a standards-compliance defect in the lexer and the downstream
codegen fragility it was hiding. Full regression suite (`bash tests/run_all_tests.sh`,
2207 tests + 149 `_fail`) = SUCCESS.

Fixes landed in this sweep:

- **Lexer: removed non-standard negative numeric literal rule** (`src/Lexer.h`).
  C++20 [lex.ccon] / [lex.fcon] produce non-negative `integer-literal` / `floating-literal`
  only — the leading `-` in `-5`, `-0.0`, `3-1`, `a-1`, `foo<3-1>`, etc. is always a
  separate token (unary or binary depending on context). The lexer's
  `c == '-' && isdigit(next)` rule eagerly combined `-<digits>` into a single literal
  token, dropping binary `-` and breaking expressions like `3-1` and non-type template
  arguments of the form `_Np - 1` (which is exactly the shape `libstdc++`'s
  `<variant>::_Variadic_union(... , in_place_index<_Np-1>, ...)` and several `<tuple>` /
  `<ratio>` helpers use).

- **Parser: constant-fold unary `+` / `-` on numeric literals at AST construction**
  (`src/Parser_Expr_PrimaryUnary.cpp`). Because the lexer no longer pre-negates,
  `-5.0` / `-0.0` / `-5` are now `UnaryOperator(-, NumericLiteralNode(...))` in the AST.
  At the point where the parser pops `pending_unary_ops` and wraps the operand, a unary
  `+` / `-` applied directly to a `NumericLiteralNode` is folded back into a single
  `NumericLiteralNode` with the negated value, preserving `TypeCategory` /
  `TypeQualifier` / size. This is standard constant folding and in particular keeps
  IEEE-754 signed-zero semantics exact: `-0.0` retains the sign bit (bit-distinct from
  `+0.0`), which the codegen-via-`0.0 - x` alternative would not. It also avoids the
  pre-existing `handleUnaryOperation` path's lack of support for `double` IR immediates
  (the integer `NEG` instruction is emitted which is wrong for floats).

Regression tests added:

- `tests/test_subtract_literal_in_template_arg_ret0.cpp` — `3 - 1`, `a - 1`, and
  `tag<3 - 1>` in non-type template argument position.
- `tests/test_unary_minus_literal_fold_ret0.cpp` — integer literals (including
  `-9223372036854775807LL - 1` for `INT64_MIN`), `double` / `float` literals including
  `-0.0`, contextual-bool conversion of `-0.0`, `-(-x)` double-negation, and unsigned
  wrap-around (`-1u == 0xFFFFFFFFu`).

Std-header status after this sweep (Linux/libstdc++-14, `x64/Sharded/FlashCpp`, 60 s
timeout, measured on the same host as previous sweeps):

- Compiles cleanly (13, unchanged): `<aggregate_brace_elision_follow>` (~41ms),
  `<bit>` (~504ms), `<compare_ret42>` (~31ms), `<concepts>` (~413ms),
  `<exception>` (~459ms), `<limits>` (~1477ms), `<new>` (~58ms),
  `<pair_swap_deleted_member>` (~25ms), `<source_location>` (~43ms),
  `<span>` (~45ms), `<type_traits_is_integral_any_of_fail>`,
  `<typeinfo_ret0>` (~60ms), `<version>` (~43ms).
- Timings (re-measured against this binary) for the headers still failing with
  compile/codegen errors (rc=1):
  `<algorithm>` ~2668ms, `<any>` ~508ms, `<array>` ~1427ms, `<atomic>` ~1018ms,
  `<chrono>` ~10418ms, `<cmath>` ~5853ms, `<deque>` ~3416ms, `<fstream>` ~6805ms,
  `<functional>` ~2495ms, `<iostream>` ~6749ms, `<iterator>` ~2851ms,
  `<latch>` ~1022ms, `<list>` ~2683ms, `<map>` ~2447ms, `<memory>` ~3123ms,
  `<numeric>` ~2267ms, `<optional>` ~1023ms, `<optional_codegen_recovery>` ~1026ms,
  `<queue>` ~3506ms, `<ranges>` ~3062ms, `<ratio>` ~504ms, `<set>` ~2461ms,
  `<shared_mutex>` ~2690ms, `<sstream>` ~6744ms, `<stack>` ~3410ms,
  `<stdexcept>` ~6542ms, `<string>` ~6480ms, `<string_view>` ~2898ms,
  `<tuple>` ~1979ms, `<type_traits>` ~329ms, `<utility>` ~693ms, `<vector>` ~1901ms,
  `<wstring_view_find_ret0>` ~2646ms.
- **New regression exposed (not introduced by these fixes, but previously masked by
  the `_Np-1` parse error):** `<variant>` (`tests/std/test_std_variant.cpp`) now
  progresses past the `_Variadic_union` member-initializer and reaches codegen, where
  it SIGSEGVs reliably (~2450ms, rc=139). Previously the parse error aborted the
  compile before this code path was exercised. The crash is a pre-existing
  codegen/sema bug in the `<variant>` visit machinery; opening as a follow-up item
  rather than rolling back the standards-compliance fix.

Notable first-order error changes enabled by this sweep (not new fixes — just that
the deeper error surface is now reachable):

- `<variant>` — was "`Expected ')' after initializer arguments`" at
  `<variant>:418` from the `in_place_index<_Np-1>` arithmetic non-type argument;
  now reaches codegen (see crash above).
- `<ratio>` — `ratio_less` / `ratio_greater` static asserts that used to stop at
  `_Np - 1`-style constant folds in chained comparisons now proceed to the next
  layer; the remaining failure is evaluating qualified-identifier constants in the
  targeted `static_assert`.
- `<tuple>`, `<chrono>`, `<algorithm>` — various internal helpers that write
  `N - 1` / `sizeof...(Args) - 1` in non-type template argument position no longer
  mis-tokenize, so their downstream errors (e.g. `_M_tail`, `Cannot use copy
  initialization with explicit constructor`) are now the first-order errors
  attributable to the real bug rather than an early lexer artifact.

#### 2026-04-24 Preprocessor / Typedef / Builtin Sweep (Linux/libstdc++-14)

This sweep re-ran every `tests/std/test_std_*.cpp` against system libstdc++-14 with the freshly
rebuilt `x64/Sharded/FlashCpp` and identified the actual first-order compile error per header
(the previous sweep's bucket-by-first-error was masked by the SFINAE path logging
"Non-type parameter not supported in deduction" at Error level for every overload attempt).
Full regression suite `tests/run_all_tests.sh` (2205 tests, 149 `_fail`) = SUCCESS.

Fixes landed in this sweep:

- **Preprocessor: `__has_feature` / `__has_extension` / `__building_module` etc.** —
  Clang's `<stddef.h>` and its `__stddef_size_t.h` / `__stddef_ptrdiff_t.h` / … family
  gate the `typedef __SIZE_TYPE__ size_t;` declarations on
  `#if !defined(_SIZE_T) || __has_feature(modules)`. FlashCpp's preprocessor had no handler
  for `__has_feature` — the unknown identifier silently pushed 0, then the trailing
  `(arg)` was parsed as a separate parenthesized subexpression, producing a stack imbalance
  that turned the whole `#if` into 0. The typedef bodies were therefore entirely skipped,
  so `size_t` was never declared, so `sizeof(size_t)` returned 0, so glibc's
  `<bits/types/struct_FILE.h>` aborted with "Invalid array bound for member `_unused2`:
  sizeof evaluated to 0 for type `size_t`". Added explicit handlers for `__has_feature`,
  `__has_extension`, `__building_module`, `__is_target_arch`, `__is_target_vendor`,
  `__is_target_os`, `__is_target_environment`, `__is_identifier`; each correctly consumes
  the `(arg)` and pushes 0 (or 1 for `__is_identifier`). File: `src/FileReader_Macros.cpp`.
  Unblocked: `<chrono>`, `<fstream>`, `<iostream>`, `<sstream>`, `<stdexcept>`, `<string>`
  past the struct_FILE.h size-0 blocker.

- **Parser: C-style function-type typedefs with parameter lists** —
  `typedef ssize_t cookie_read_function_t(void *, char *, size_t);` (glibc's
  `<bits/types/cookie_io_functions_t.h>`) failed with "Expected ';' after typedef declaration".
  Root cause: `Parser::parse_typedef` explicitly `advance()`'d past the `(` before calling
  `skip_balanced_parens()`, which *requires* the `(` still be on the stream — so it
  immediately returned and the parameter list stayed unconsumed. Removed the erroneous
  `advance()`. Regression: `tests/test_typedef_function_type_multiparam_ret0.cpp`.
  File: `src/Parser_Decl_TypedefUsing.cpp`.

- **Builtin registration: `__builtin_alloca` / `__builtin_alloca_with_align`** —
  `<bits/locale_facets.h>` / `<bits/locale_facets_nonio.tcc>` use the unqualified names
  from template bodies, hitting the C++20 [temp.res]/9 "non-dependent name … was not
  declared before the template definition" rule because the builtin names were not
  visible at definition time. Registered both as `extern "C"` builtins returning `void*`.
  Also added them to the `__has_builtin` supported set. Files: `src/Parser_Core.cpp`,
  `src/FileReader_Macros.cpp`.

- **Diagnostics: demote spurious "Non-type parameter not supported in deduction" error** —
  This is a normal SFINAE outcome (the caller returns `nullopt` to try the next overload)
  and was being logged at Error level for every overload attempt across every header,
  masking the real first-order error. Now logs at Debug with the actual parameter name.
  File: `src/Parser_Templates_Inst_Deduction.cpp`.

Current first-order errors after this sweep (grouped by root cause, not just per-header):

- **(6 headers) `<chrono>`, `<fstream>`, `<iostream>`, `<sstream>`, `<stdexcept>`, `<string>`** —
  `/usr/include/c++/14/bits/basic_string.h:4699` "Template instantiation failed or type not
  found" in the `operator""s` user-defined-literal family returning `basic_string<CharT>`.
  Was previously masked by the `struct_FILE.h` size-0 blocker.
- **(4 headers) `<functional>`, `<list>`, `<memory>`, `<tuple>`** —
  `/usr/include/c++/14/tuple:399` "No matching function for call to `_M_tail`" in
  `_Tuple_impl`'s allocator-extended copy constructor: unqualified call to the
  static member `_M_tail` inside a member-initializer list isn't resolving to the
  same class's member.
- **(4 headers) `<iterator>`, `<map>`, `<ranges>`, `<set>`** —
  `/usr/include/c++/14/bits/stl_pair.h:308` or `node_handle.h:285` "Call to deleted
  function `swap`": overload resolution picks the SFINAE'd-out `swap(pair<_T1,_T2>&,
  pair<_T1,_T2>&) = delete;` rather than the earlier non-deleted overload, because
  `enable_if_t<!__and_<__is_swappable<_T1>, __is_swappable<_T2>>::value>::type` isn't
  fully removing the deleted candidate during overload resolution.
- **(3 headers) `<deque>`, `<queue>`, `<stack>`** — `stl_deque.h:700` non-dependent name
  `_M_deallocate_node` in out-of-line template member function body not finding its
  own class's member function during unqualified lookup.
- **(3 headers) `<algorithm>`, `<array>`, `<string_view>`** — "Cannot use copy
  initialization with explicit constructor for target type `std::reverse_iterator`": the
  return statement in `array::rbegin()` is being treated as copy-init rather than the
  constructor-call expression it is.
- **(1 header)** `<cmath>` — `tr1/poly_hermite.tcc:121` non-dependent name
  `__poly_hermite_recursion` despite being declared at line 74 of the same file.
- **(1 header)** `<atomic>` — `atomic:92` No matching member function for call to `load`.
- **(1 header)** `<vector>` — "Itanium name mangling: unknown type".
- **(1 header)** `<variant>` — `variant:418` Expected ')' after initializer arguments.
- **(1 header)** `<shared_mutex>` — "struct info not found for constructor call type
  `time_point`".
- **(2 tests)** `<optional>` / `<optional_codegen_recovery>` — "struct type info not found".
- **(1 test)** `<ratio>` — `static_assert` involves `std::ratio_less` which evaluates to
  "Undefined qualified identifier in constant expression".
- **(1 test)** `<type_traits>` — `static_assert(std::is_integral<int>::value)` fails.
- **(1 test)** `<wstring_view_find_ret0>` — No matching member function for call to `find`.

Post-sweep std-header pass count (unchanged at 13/47, but several headers now reach a
later, more actionable error rather than a generic SFINAE stop):
`<aggregate_brace_elision_follow>`, `<bit>`, `<compare_ret42>`, `<concepts>`,
`<exception>`, `<limits>`, `<new>`, `<pair_swap_deleted_member>`, `<source_location>`,
`<span>`, `<type_traits_is_integral_any_of_fail>`, `<typeinfo_ret0>`, `<version>`.

Regression note: the table rows claiming `<atomic>`, `<latch>`, `<variant>` pass on
2026-04-24 were verified this sweep against `origin/main` (commit `a4070ae`, before any
of this sweep's changes) — they fail there too. The table rows are stale from the
earlier 2026-04-24 depth-guard sweep and should be read against the Post-Depth-Guard
Sweep's "cleanly compiles (13)" list and this sweep's updated first-error bucket.

#### 2026-04-24 Post-Depth-Guard Sweep (Linux/libstdc++-14)

After the initial 2026-04-24 depth guard landed, a subsequent sweep showed most std headers were actually SIGSEGV'ing again. Investigation with gdb pinpointed the real root cause and produced a more complete fix:

- **Root cause (fixed this session):** `Parser::substitute_template_parameter`'s nested helper `resolveConcreteTemplateArgPlaceholders` (`src/Parser_Expr_QualLookup.cpp`) self-recursed on cyclic template-instantiation placeholder arg graphs with no visited-set or depth guard. libstdc++'s `__normal_iterator` / `iterator_traits` SFINAE chains drove it into the millions of frames, exhausting the 16MB thread stack. Fix: track visited placeholder `TypeIndex` values for the duration of a single `substitute_template_parameter` call and bail out on revisit.
- **Secondary recursion paths (also fixed):** `try_instantiate_class_template` and `reparse_template_function_body` (both in `src/Parser_Templates_Inst_Deduction.cpp` / `src/Parser_Templates_Inst_ClassTemplate.cpp`) plus `instantiate_member_function_template_core` (`src/Parser_Templates_Inst_MemberFunc.cpp`) all had recursion paths that could blow the stack even with the above fix. Each now has a tight per-thread depth guard (24) that returns `std::nullopt` / no-op cleanly instead of crashing. The existing `try_instantiate_class_template` iteration-count guard used to reset itself after firing, so a single runaway compilation could produce millions of retries; it is now sticky for the remainder of the compilation.
- **Post-fix std header status** (with libstdc++-14 headers, `x64/Sharded/FlashCpp`, 45 s timeout, running against `tests/std/test_std_*.cpp`):
  - Compiles cleanly (13): `<aggregate_brace_elision_follow>`, `<bit>`, `<compare_ret42>`, `<concepts>`, `<exception>`, `<limits>`, `<new>`, `<pair_swap_deleted_member>`, `<source_location>`, `<span>`, `<type_traits_is_integral_any_of_fail>`, `<typeinfo_ret0>`, `<version>`.
  - Compile/codegen errors (34) — previously all crashed with SIGSEGV on `#include`, now reach clean error exits and are therefore investigable: `<algorithm>` (~1.9 s), `<any>` (~0.4 s), `<array>` (~1.0 s), `<atomic>` (~0.7 s), `<chrono>` (~7.1 s), `<cmath>` (~4.1 s), `<deque>` (~2.5 s), `<fstream>` (~4.8 s), `<functional>` (~1.8 s), `<iostream>` (~4.9 s), `<iterator>` (~2.0 s), `<latch>` (~0.7 s), `<list>` (~1.9 s), `<map>` (~1.8 s), `<memory>` (~2.2 s), `<numeric>` (~1.5 s), `<optional>` (~0.7 s), `<optional_codegen_recovery>` (~0.7 s), `<queue>` (~2.5 s), `<ranges>` (~2.1 s), `<ratio>` (~0.4 s), `<set>` (~1.8 s), `<shared_mutex>` (~1.8 s), `<sstream>` (~4.8 s), `<stack>` (~2.5 s), `<stdexcept>` (~4.7 s), `<string>` (~4.7 s), `<string_view>` (~2.1 s), `<tuple>` (~1.4 s), `<type_traits>` (~0.2 s), `<utility>` (~0.5 s), `<variant>` (~0.7 s), `<vector>` (~1.4 s), `<wstring_view_find_ret0>` (~1.9 s).
  - Crashes: **0** (down from 22).
  - Timeouts: **0** (down from 4 — the iteration-count guard's sticky-tripped bit is what closed the retry-storm pattern for `<iterator>` / `<map>` / `<ranges>` / `<set>`).
- Full regression suite (`bash tests/run_all_tests.sh`, 2204 tests + 149 _fail): SUCCESS.

The remaining 34 failures are now exposed first-order compile errors (most logged early as "Non-type parameter not supported in deduction" before the header progresses further) rather than stack-overflow crashes masking them. These are unblocked for targeted investigation in follow-up work.



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

### Recent Fixes (2026-04-30)

1. **Sema models pointer arithmetic in `inferExpressionType`**: previously every binary operator with a pointer operand returned `nullopt`, so a sema-normalized ternary whose branches involved pointer arithmetic could not compute a sema-owned result type.  After the recent commit that made codegen require an exact ternary result type for normalized function bodies, this regressed `<typeinfo>`'s `name()` member, whose body is exactly `return __name[0] == '*' ? __name + 1 : __name;`.  Sema now follows C++20 [expr.add]: `T* + integral` and `integral + T*` yield `T*`, `T* - integral` yields `T*`, and `T* - T*` yields `ptrdiff_t`.  Regression: `tests/test_ternary_pointer_arithmetic_branches_ret0.cpp`.  Std-header impact: `tests/std/test_std_typeinfo_ret0.cpp` passes again on Linux/libstdc++-14 in ~46ms.

### Recent Fixes (2026-04-24, post-depth-guard)

1. **Cyclic template-instantiation placeholder arg graphs no longer cause unbounded recursion**: `Parser::substitute_template_parameter`'s inner helper `resolveConcreteTemplateArgPlaceholders` (`src/Parser_Expr_QualLookup.cpp`) now carries a visited `std::unordered_set<TypeIndex>` across its self-recursion. When a placeholder's template argument transitively references the same placeholder type (the common libstdc++ pattern is `iterator_traits<__normal_iterator<P, C>>` whose arg `__normal_iterator<P, C>` internally re-references the same traits chain), the recursion now stops at the first revisit instead of running until the thread stack is exhausted. This single fix is what turns the `SIGSEGV` crashes on bare `#include <atomic>`, `#include <bit>`, `#include <exception>`, `#include <concepts>`, and most large libstdc++ containers into clean compile errors (or clean compiles in a few cases).

2. **Three tight per-thread depth guards added on the template-instantiation entry points** — `try_instantiate_class_template` (already existed, lowered from 256 to 24 to match observed frame size on Linux's 16MB stack), `reparse_template_function_body` (new, 24), and `instantiate_member_function_template_core` (new, 24). Each is gated by its own sticky "already warned" flag so a runaway recursion only produces one diagnostic per compilation instead of tens of thousands. Together they catch the recursion chains that do not flow through `substitute_template_parameter` (e.g. container headers that re-enter member-template body replay for every overload resolution).

3. **`try_instantiate_class_template`'s iteration-count guard is now sticky for the rest of the compilation** instead of auto-resetting itself. Previously, once the 10,000-call cap was hit, the counter reset to zero, causing callers that treat `std::nullopt` as "try another overload" to burn through millions of retries in what looked externally like a hang. The guard now trips once and stays tripped, converting the previous `<iterator>` / `<map>` / `<ranges>` / `<set>` 45 s timeouts into ~2 s clean compile errors.

4. **Net effect on the std-header sweep**: `tests/std/test_std_*.cpp` against libstdc++-14 goes from 22 crashes + 4 timeouts + 13 compiles down to **0 crashes + 0 timeouts + 13 compiles + 34 clean compile errors**. Main regression suite (`bash tests/run_all_tests.sh`): 2204/2204 pass, 149/149 `_fail` correct.

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
