# Standard Header Tests

This directory contains test files for C++ standard library headers to assess FlashCpp's compatibility with the C++ standard library.

## Current Status

| Header | Test File | Status | Notes |
|--------|-----------|--------|-------|
| `<limits>` | `test_std_limits.cpp` | ✅ Compiled | ~153ms |
| `<type_traits>` | `test_std_type_traits.cpp` | ✅ Compiled | ~196ms |
| `<compare>` | `test_std_compare_ret42.cpp` | ✅ Compiled | ~20ms (targeted retest 2026-03-31) |
| `<version>` | `test_std_version.cpp` | ✅ Compiled | ~15ms |
| `<source_location>` | `test_std_source_location.cpp` | ✅ Compiled | ~3ms |
| `<numbers>` | N/A | ✅ Compiled | ~194ms |
| `<initializer_list>` | N/A | ✅ Compiled | ~16ms |
| `<ratio>` | `test_std_ratio.cpp` | ✅ Compiled | ~600ms (targeted retest 2026-04-05). The header still compiles, but `std::ratio_less` remains blocked because non-type default template arguments that depend on qualified constexpr members (for example `__ratio_less_impl`'s bool defaults) are still not fully instantiated/evaluated. |
| `<optional>` | `test_std_optional.cpp` | ❌ Codegen Error | ~1400ms (targeted retest 2026-04-03). Chained member-access IR now keeps the concrete `_Optional_payload<...>` type index instead of collapsing back to `type_index=0`, but libstdc++ still fails later because one `_Optional_payload<...>` path is not recovered as a struct during deferred member codegen and inherited `_M_engaged` lookups still fail. |
| `<any>` | `test_std_any.cpp` | ✅ Compiled | ~271ms |
| `<utility>` | `test_std_utility.cpp` | ✅ Compiled | ~356ms |
| `<concepts>` | `test_std_concepts.cpp` | ✅ Compiled | ~220ms |
| `<bit>` | `test_std_bit.cpp` | ✅ Compiled | ~237ms |
| `<string_view>` | `test_std_string_view.cpp` | ❌ Codegen Error | ~3690ms (targeted retest 2026-04-03). The earlier `wmemchr` ambiguity is fixed; current failures are still downstream ranges/iterator follow-ons (`Operator==` / `Operator-`), `__as_const` / reference-size placeholder issues, and later sema/codegen conversion gaps. |
| `<string>` | `test_std_string.cpp` | 💥 Crash | ~10220ms (targeted retest 2026-04-06). Qualified direct-init arguments such as `_Alloc_traits::_S_select_on_copy(__a)` now parse correctly inside `std::__str_concat`, so `<string>` gets past the old phase-1 `__str` stop in `basic_string.h`. The current Linux repro now runs much deeper into `basic_string` / iterator instantiation before segfaulting. |
| `<array>` | `test_std_array.cpp` | ❌ Codegen Error | ~1360ms (targeted retest 2026-04-03). The aggregate brace-init blocker stays fixed and the old array-alias regression tests remain green; current failures are still later iterator/ranges follow-ons (`Operator-` via `operator+`, `make_move_iterator`, `operator==`, and placeholder-sized helper types). |
| `<algorithm>` | `test_std_algorithm.cpp` | ❌ Compile Error | ~2710ms (targeted retest 2026-04-03). Now gets well into ranges/view lowering, but still fails later on `_S_empty` / `_S_size`, iterator comparisons/arithmetic (`Operator==`, `Operator-`, `Operator<` / `Operator>`), and an eventual unresolved-`auto` mangling path. |
| `<span>` | `test_std_span.cpp` | ❌ Codegen Error | Fatal error: Operator- not defined for operand types (previously `__detail::__extent_storage` primary template not found) |
| `<tuple>` | `test_std_tuple.cpp` | 💥 Crash | ~1960ms (targeted retest 2026-04-06). Deferred `_Tuple_impl<I + 1, Rest...>` base arguments with non-type expressions now substitute/evaluate instead of stopping at the old post-parse `PackExpansionExprNode` boundary, but the current run still falls later into `_Head_base` default-non-type evaluation / repeated deferred-base recursion and segfaults. |
| `<vector>` | `test_std_vector.cpp` | ❌ Compile Error | ~7830ms (targeted retest 2026-04-03). Base-member lookup is no longer the first stop; the current Linux repro now gets much deeper into `__copy_move_a` / `__copy_move_backward_a` / relocation helpers before failing on non-type deduction gaps, static-assert follow-ons, and a later unknown-type Itanium mangling abort. |
| `<deque>` | `test_std_deque.cpp` | ❌ Codegen Error | Itanium mangling: unresolved 'auto' type reached mangling |
| `<list>` | `test_std_list.cpp` | ❌ Codegen Error | member '_M_impl' not found in struct 'std::__cxx11::list' |
| `<queue>` | `test_std_queue.cpp` | ❌ Codegen Error | Itanium mangling: unresolved 'auto' type reached mangling |
| `<stack>` | `test_std_stack.cpp` | ❌ Codegen Error | Itanium mangling: unresolved 'auto' type reached mangling |
| `<memory>` | `test_std_memory.cpp` | ❌ Codegen Error | Itanium mangling: unresolved 'auto' type reached mangling |
| `<functional>` | `test_std_functional.cpp` | ❌ Codegen Error | Itanium mangling: unresolved 'auto' type reached mangling |
| `<map>` | `test_std_map.cpp` | ❌ Codegen Error | member 'first' not found in struct 'std::iterator' |
| `<set>` | `test_std_set.cpp` | ❌ Codegen Error | Itanium mangling: unresolved 'auto' type reached mangling |
| `<ranges>` | `test_std_ranges.cpp` | 💥 Crash | ~12960ms (targeted retest 2026-04-06). The earlier `streamoff`, `size_t(-1)`, and alias-constructor fixes still hold, and qualified direct-init parsing now also gets past the inherited `std::__str_concat` / `__str` stop from `<string>`. The current Linux repro now crashes later after much deeper `basic_string` / iterator instantiation. |
| `<iostream>` | `test_std_iostream.cpp` | 💥 Crash | ~4760ms (targeted retest 2026-04-02). The earlier `wmemchr` ambiguity is fixed and it gets much further, but still hits later ranges/string-view issues (`Operator-`, `make_move_iterator`, unresolved `auto`) before crashing in `IROperandHelpers::toIrValue` |
| `<sstream>` | `test_std_sstream.cpp` | ❌ Codegen Error | char_traits member functions not found during deferred body codegen |
| `<fstream>` | `test_std_fstream.cpp` | ❌ Codegen Error | char_traits member functions not found during deferred body codegen |
| `<chrono>` | `test_std_chrono.cpp` | ❌ Compile Error | ~4380ms (targeted retest 2026-04-05). Leading-identifier statement disambiguation still gets past the old `duration::operator+=` / `operator-=` parse stop in `bits/chrono.h`; the current blocker remains the later non-dependent-name error for `__time_point`, with repeated ratio/time_point static-assert fallout still showing up beforehand. |
| `<atomic>` | `test_std_atomic.cpp` | ❌ Compile Error | ~830ms (targeted retest 2026-04-05). Namespace-qualified dependent explicit function-template calls now instantiate with call-argument deduction, and `__builtin_memcpy` is registered/remapped, so `<atomic>` gets past the old `std::__atomic_impl::__compare_exchange<_AtomicRef>` / builtin stop. The current blocker is a later `std::__atomic_impl::store` path that still ends in `Itanium name mangling: unknown type`. |
| `<new>` | `test_std_new.cpp` | ✅ Compiled | ~24ms |
| `<exception>` | `test_std_exception.cpp` | ✅ Compiled | ~238ms |
| `<stdexcept>` | `test_std_stdexcept.cpp` | 💥 Crash | ~12260ms (targeted retest 2026-04-06). Same new state as `<string>`: alias-based constructor calls and qualified direct-init arguments now parse correctly, so the old `wstring(...)` and later `std::__str_concat` / `__str` stops are gone. The current Linux repro now crashes later after much deeper `basic_string` / iterator instantiation. |
| `<typeinfo>` | N/A | ✅ Compiled | ~32ms |
| `<typeindex>` | N/A | ✅ Compiled | ~284ms |
| `<numeric>` | `test_std_numeric.cpp` | ✅ Compiled | ~632ms |
| `<iterator>` | `test_std_iterator.cpp` | ✅ Compiled | ~1669ms (some codegen warnings) |
| `<variant>` | `test_std_variant.cpp` | ❌ Codegen Error | ~1200ms (targeted retest 2026-04-03). The earlier post-parse `PackExpansionExprNode` blocker is gone; libstdc++ now gets through the pattern-struct boundary and fails later on variant visitation instantiation / Itanium mangling (`unknown type`). |
| `<csetjmp>` | N/A | ✅ Compiled | ~18ms |
| `<csignal>` | N/A | ✅ Compiled | ~103ms |
| `<stdfloat>` | N/A | ✅ Compiled | ~3ms (C++23) |
| `<spanstream>` | N/A | ✅ Compiled | ~24ms (C++23) |
| `<print>` | N/A | ✅ Compiled | ~28ms (C++23) |
| `<expected>` | N/A | ✅ Compiled | ~41ms (C++23) |
| `<text_encoding>` | N/A | ✅ Compiled | ~26ms (C++26) |
| `<stacktrace>` | N/A | ✅ Compiled | ~25ms (C++23) |
| `<barrier>` | N/A | 💥 Crash | Stack overflow during template instantiation |
| `<coroutine>` | N/A | ❌ Parse Error | Requires `-fcoroutines` flag |
| `<latch>` | `test_std_latch.cpp` | ❌ Compile Error | ~840ms (targeted retest 2026-04-05). Shares the new `<atomic>` state: qualified-template deduction and `__builtin_memcpy` both work now, and the current failure has moved later to the same `std::__atomic_impl::store` / unknown-type mangling path. |
| `<shared_mutex>` | `test_std_shared_mutex.cpp` | ❌ Codegen Error | ~1440ms (targeted retest 2026-04-05). The old chrono parse blocker is still gone; it now gets through the chrono stack and further into codegen before failing in `_S_from_sys` on missing symbol `_S_epoch_diff` rather than the older `denorm_absent` stop. |
| `<cstdlib>` | N/A | ✅ Compiled | ~76ms |
| `<cstdio>` | N/A | ✅ Compiled | ~43ms |
| `<cstring>` | N/A | ✅ Compiled | ~39ms |
| `<cctype>` | N/A | ✅ Compiled | ~33ms |
| `<cwchar>` | N/A | ✅ Compiled | ~41ms |
| `<cwctype>` | N/A | ✅ Compiled | ~44ms |
| `<cerrno>` | N/A | ✅ Compiled | ~15ms |
| `<cassert>` | N/A | ✅ Compiled | ~15ms |
| `<cstdarg>` | N/A | ✅ Compiled | ~13ms |
| `<cstddef>` | N/A | ✅ Compiled | ~33ms |
| `<cstdint>` | N/A | ✅ Compiled | ~17ms |
| `<cinttypes>` | N/A | ✅ Compiled | ~22ms |
| `<cuchar>` | N/A | ✅ Compiled | ~48ms |
| `<cfenv>` | N/A | ✅ Compiled | ~15ms |
| `<clocale>` | N/A | ✅ Compiled | ~20ms |
| `<ctime>` | N/A | ✅ Compiled | ~34ms |
| `<climits>` | N/A | ✅ Compiled | ~14ms |
| `<cfloat>` | N/A | ✅ Compiled | ~14ms |
| `<cmath>` | `test_std_cmath.cpp` | ✅ Compiled | ~3743ms |
| `<system_error>` | N/A | ✅ Compiled | (some codegen warnings) |
| `<scoped_allocator>` | N/A | ✅ Compiled | |
| `<charconv>` | N/A | ✅ Compiled | |
| `<numbers>` | N/A | ✅ Compiled | |
| `<mdspan>` | N/A | ✅ Compiled | (C++23) |
| `<flat_map>` | N/A | ✅ Compiled | (C++23) |
| `<flat_set>` | N/A | ✅ Compiled | (C++23) |
| `<unordered_set>` | N/A | ❌ Codegen Error | struct type info not found (parsing succeeds) |
| `<unordered_map>` | N/A | ❌ Codegen Error | Cannot use implicit conversion with explicit std::pair constructor |
| `<mutex>` | N/A | ❌ Parse Error | Expected primary expression |
| `<condition_variable>` | N/A | 💥 Crash | Stack overflow |
| `<thread>` | N/A | ❌ Parse Error | Expected ')' after if condition |
| `<semaphore>` | N/A | ❌ Parse Error | Expected ')' after if condition |
| `<stop_token>` | N/A | ❌ Parse Error | Expected ')' after if condition |
| `<bitset>` | N/A | ❌ Parse Error | Expected identifier token |
| `<execution>` | N/A | ❌ Parse Error | Expected ';' after for loop initialization (forward type ref) |
| `<generator>` | N/A | ❌ Parse Error | Ambiguous call to `__to_unsigned_like` (C++23) |

**Legend:** ✅ Compiled | ❌ Failed/Parse/Include Error | 💥 Crash

### Summary (2026-03-14, updated)

**Total headers tested:** 96
**Compiling successfully (parse + codegen):** 55 (57%)
**Codegen errors (parsing succeeds but codegen fails):** 18 (including many headers now failing with Itanium mangling issues after alloc_traits.h fix unblocked parsing)
**Parse errors:** 10
**Crashes:** 4 in the last full sweep (deep template-instantiation paths; see the targeted retest note below for newer per-header updates)

**Targeted retest note (2026-04-06):** `<tuple>` was re-checked after teaching deferred template-base instantiation to substitute/evaluate non-type expression arguments such as `_Tuple_impl<_Idx + 1, _Tail...>`. That clears the previous post-parse `PackExpansionExprNode` stop and moves the header into a later failure mode, but the current Linux repro still segfaults after `_Head_base` default non-type arguments repeatedly fall back to `0`, leaving `_Head_base` deferred bases unresolved and driving much deeper `_Tuple_impl` recursion. On the same date, `<string>`, `<ranges>`, and `<stdexcept>` were retested after two parser fixes in a row: alias-based constructor parsing clears the old `wstring(...)` failure in `basic_string.h`, and qualified direct-init disambiguation now correctly treats calls such as `_Alloc_traits::_S_select_on_copy(__a)` as expressions instead of function parameter lists. That removes the later `std::__str_concat` phase-1 `__str` stop and moves all three headers into deeper `basic_string` / iterator-instantiation crashes. The earlier 2026-04-05 targeted retest results still apply for `<atomic>`, `<latch>`, `<ratio>`, `<chrono>`, and `<shared_mutex>`, and the 2026-04-04 retest results still apply for `<optional>`, `<variant>`, `<string_view>`, `<array>`, `<algorithm>`, and `<vector>`. The overall header counts above still reflect the older full sweep and need a future comprehensive rerun before they are updated.

### Known Blockers

The most impactful blockers preventing more headers from compiling, ordered by impact:

1. **Template deduction / semantic follow-on failures after the earlier mangling blockers**: In the 2026-03-31 targeted retest, `<algorithm>` no longer failed first on unresolved- `auto` mangling. It now gets further and then fails on concepts/ranges diagnostics followed by explicit-constructor copy-initialization errors. The same family of deeper issues likely explains several headers previously bucketed under the stale unresolved- `auto` note.

2. **Function pointer signature propagation through template instantiation metadata (fixed 2026-03-31)**: Function pointer signatures were being dropped in lazy member instantiation, outer-template bindings, and free-function template instantiation. This previously caused the Itanium mangler to crash on headers such as `<string>` and `<stdexcept>`. After the fix, those headers progress past the mangling crash and now fail later on unrelated issues.

   - Core fix areas: `Parser_Templates_Lazy.cpp`, `Parser_Templates_Inst_Deduction.cpp`, and `TypeInfo::TemplateArgInfo` / outer-template binding serialization.
   - Regression test: `tests/test_funcptr_lazy_member_signature_ret0.cpp`.

3. **Deferred template-base placeholder materialization / inherited-member follow-ons**: Some dependent base arguments now materialize correctly, and chained member access no longer immediately erases concrete payload types back to `type_index=0`, but later CRTP/deferred-body codegen still has remaining gaps where instantiated payload structs are not always recovered as full structs and inherited members are not fully reconstructed. `<optional>` now reaches the later `_Optional_payload<...>` / `_M_engaged` failures with concrete type index `2758`, but those deferred paths still lose struct info or inherited `_M_engaged` lookup during codegen.

4. **Late atomic implementation mangling follow-on after the qualified-template / builtin fixes**: Pointer-style `__atomic_add_fetch` / `__atomic_fetch_sub`, namespace-qualified explicit function-template calls such as `std::__atomic_impl::__compare_exchange<_AtomicRef>(...)`, and `__builtin_memcpy` all now resolve. `<atomic>` / `<latch>` still fail later when `std::__atomic_impl::store` reaches an `Itanium name mangling: unknown type` abort, so another dependent-type/mangling gap remains deeper in the atomic implementation path. Affects: `<atomic>`, `<latch>`.

5. **Late chrono/time-point semantic follow-ons after the statement-disambiguation fix**: `<chrono>` no longer stops first on `duration::operator+=` / `operator-=` being misparsed as declarations when the namespace-scope alias `__r` shadows the member name. The next blocker is deeper template/sema handling around `chrono::time_point`, including a non-dependent-name `__time_point` error and repeated ratio/time-point static-assert fallout; `<shared_mutex>` now rides that stack further and then fails later in codegen on `_S_epoch_diff`. Affects: `<chrono>`, `<shared_mutex>`.

6. **Iterator / ranges downstream follow-on failures after the latest operator fixes**: The simple free-operator-template gap is fixed, but libstdc++ headers still hit later failures around iterator arithmetic / comparisons (`Operator-`, `Operator!=`, `_S_empty`, `_S_size`), `make_move_iterator`, and missing struct type info for some helper types. Affects: `<string_view>`, `<array>`, `<algorithm>`, `<vector>`, `<iostream>`.

7. **Variant visitation / mangling follow-ons after the pattern-struct boundary fix**: `<variant>` no longer stops on unexpanded `PackExpansionExprNode` nodes from parser-owned `$pattern__` structs, but it now exposes later template-instantiation gaps around `__get`, `__emplace`, `_S_apply_all_alts`, and an eventual Itanium mangling `unknown type` abort. Affects: `<variant>`.

8. **Ambiguous overload resolution**: `__to_unsigned_like` in ranges has multiple overloads that the overload resolver treats as ambiguous. Affects: `<ranges>`.

9. **Stack overflow during deep template instantiation**: Headers like `<barrier>`, and `<chrono>` trigger 6000-7500+ template instantiations that exhaust the stack. Affects: `<barrier>`, `<chrono>`, `<condition_variable>`.

10. **Base class member access in codegen**: Generated code fails to find members inherited from base classes (e.g., `_M_start` in `_Vector_impl`, `_M_impl` in `list`, `first` in `iterator`). Affects: `<vector>`, `<list>`, `<map>`.

11. **Late iostream-family codegen / IR lowering crash**: After the InstantiationContext fix below, `<iostream>` gets through parsing and much deeper into codegen before crashing in `IROperandHelpers::toIrValue` after `_S_empty`/`_S_size`/`move` failures. `<sstream>` / `<fstream>` still need targeted retests to see whether they now fail in the same later phase. Affects: `<iostream>`, likely `<sstream>`, `<fstream>`.

### Recent Fixes (2026-04-06)

1. **Constructor-style parsing now treats struct/class typedefs and using-aliases as temporary object construction instead of plain function-call lookup**: aliases such as `PairAlias(1, 2)` and libstdc++'s `wstring(__s.data(), __s.data() + __s.size())` now resolve through the class constructor path, including aliases that refer to template instantiations and aliases found through enclosing namespaces. This moves `<string>`, `<ranges>`, and `<stdexcept>` past the old `No matching function for call to 'wstring'` blocker. Regression tests: `tests/test_type_alias_struct_paren_ctor_ret0.cpp`, `tests/test_type_alias_template_instantiation_paren_ctor_ret0.cpp`.

2. **Deferred template-base instantiation now substitutes/evaluates non-type expression arguments before instantiating the base**: recursive bases such as `_Tuple_impl<_Idx + 1, _Tail...>` no longer leave the raw expression argument unresolved during deferred-base replay, which moves `<tuple>` past its earlier post-parse pack-expansion boundary into a later `_Head_base` / recursion crash. Regression test: `tests/test_deferred_template_base_non_type_expr_ret0.cpp`.

3. **Declaration-vs-direct-initialization disambiguation now keeps qualified static/member calls as expression arguments even when they start with a known type name**: local declarations such as `Arg arg(Helper::make(7));` no longer get misread as function declarations just because `Helper` is a known type and the initializer begins with `Helper::...`. This fixes the `std::__str_concat` local `_Str __str(_Alloc_traits::_S_select_on_copy(__a));` parse path and moves `<string>`, `<ranges>`, and `<stdexcept>` beyond the old phase-1 `__str` error to later crashes. Regression test: `tests/test_qualified_static_call_direct_init_ret0.cpp`.

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
