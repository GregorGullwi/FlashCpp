# Standard Header Tests

This directory contains test files for C++ standard library headers to assess FlashCpp's compatibility with the C++ standard library.

## Current Status

| Header | Test File | Status | Notes |
|--------|-----------|--------|-------|
| `<limits>` | `test_std_limits.cpp` | ✅ Compiled | ~118ms |
| `<type_traits>` | `test_std_type_traits.cpp` | ✅ Compiled | ~180ms |
| `<compare>` | `test_std_compare_ret42.cpp` | ✅ Compiled | ~10ms |
| `<version>` | N/A | ✅ Compiled | ~23ms |
| `<source_location>` | N/A | ✅ Compiled | ~23ms |
| `<numbers>` | N/A | ✅ Compiled | ~194ms |
| `<initializer_list>` | N/A | ✅ Compiled | ~15ms |
| `<ratio>` | `test_std_ratio.cpp` | ❌ Parse Error | Variable template evaluation (__is_ratio_v) not supported in static_assert context |
| `<optional>` | `test_std_optional.cpp` | 💥 Codegen Crash | Crashes during codegen (PackExpansionExprNode, nested member access on non-struct type) |
| `<any>` | `test_std_any.cpp` | ✅ Compiled | ~288ms |
| `<utility>` | `test_std_utility.cpp` | ✅ Compiled | ~355ms (2026-02-26: previously failed on pair member 'first'; now compiles with fold expression warnings) |
| `<concepts>` | `test_std_concepts.cpp` | ✅ Compiled | ~212ms |
| `<bit>` | N/A | ✅ Compiled | ~257ms |
| `<string_view>` | `test_std_string_view.cpp` | ✅ Compiled | ~1163ms (codegen warnings on fold expressions/pack expansion but generates .o) |
| `<string>` | `test_std_string.cpp` | 💥 Codegen Crash | Parses successfully; crashes during codegen (PackExpansionExprNode) |
| `<array>` | `test_std_array.cpp` | ❌ Parse Error | Aggregate brace initialization not supported for template types (`std::array<int,5> arr = {1,2,3,4,5}`) |
| `<algorithm>` | `test_std_algorithm.cpp` | ✅ Compiled | ~1326ms (codegen warnings on fold expressions but generates .o) |
| `<span>` | `test_std_span.cpp` | ✅ Compiled | ~895ms (codegen warnings on fold expressions but generates .o) |
| `<tuple>` | `test_std_tuple.cpp` | ✅ Compiled | ~859ms (codegen warnings on sizeof.../fold expressions but generates .o) |
| `<vector>` | `test_std_vector.cpp` | ❌ Codegen Error | Parses; member `_M_start` not found in `_Vector_impl` during codegen |
| `<memory>` | `test_std_memory.cpp` | ❌ Parse Error | (2026-02-26: sentry fix, final fix) Now fails on `_M_get_deleter` override not found in base class |
| `<functional>` | `test_std_functional.cpp` | ❌ Parse Error | Base class `__hash_code_base` not found in `<hashtable.h>` (dependent base class) |
| `<map>` | `test_std_map.cpp` | ❌ Codegen Error | Parses OK; member 'first' not found in struct 'std::iterator' during codegen |
| `<set>` | `test_std_set.cpp` | ✅ Compiled | ~1456ms (codegen warnings on sizeof... but generates .o) |
| `<ranges>` | `test_std_ranges.cpp` | ❌ Parse Error | (2026-02-26: past sentry) Now fails on brace-init in member initializer list (`std::optional<_Tp>{std::in_place}`) |
| `<iostream>` | `test_std_iostream.cpp` | 💥 Codegen Crash | (2026-02-26: past sentry) Parses further; crashes on PackExpansionExprNode |
| `<chrono>` | `test_std_chrono.cpp` | ❌ Parse Error | (2026-02-26: past sentry) Now fails on `chrono::duration<long double>{__secs}` brace-init expression |
| `<atomic>` | N/A | ✅ Compiled | ~1033ms |
| `<new>` | N/A | ✅ Compiled | ~31ms |
| `<exception>` | N/A | ✅ Compiled | ~249ms |
| `<typeinfo>` | N/A | ✅ Compiled | ~32ms |
| `<typeindex>` | N/A | ✅ Compiled | ~284ms |
| `<numeric>` | N/A | ✅ Compiled | ~557ms |
| `<variant>` | `test_std_variant.cpp` | ❌ Parse Error | Expected ';' after struct/class definition at variant:1137 |
| `<csetjmp>` | N/A | ✅ Compiled | ~18ms |
| `<csignal>` | N/A | ✅ Compiled | ~103ms |
| `<stdfloat>` | N/A | ✅ Compiled | ~3ms (C++23) |
| `<spanstream>` | N/A | ✅ Compiled | ~24ms (C++23) |
| `<print>` | N/A | ✅ Compiled | ~28ms (C++23) |
| `<expected>` | N/A | ✅ Compiled | ~41ms (C++23) |
| `<text_encoding>` | N/A | ✅ Compiled | ~26ms (C++26) |
| `<stacktrace>` | N/A | ✅ Compiled | ~25ms (C++23) |
| `<barrier>` | N/A | 💥 Codegen Crash | Crashes on PackExpansionExprNode during codegen |
| `<coroutine>` | N/A | ❌ Parse Error | Requires `-fcoroutines` flag |
| `<latch>` | `test_std_latch.cpp` | ✅ Compiled | ~633ms (codegen warning on `_Size` non-type template parameter) |
| `<shared_mutex>` | N/A | ❌ Parse Error | Fails on `chrono::duration<long double>{__secs}` brace-init expression (via `<chrono>`) |
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
| `<cmath>` | `test_std_cmath.cpp` | ✅ Compiled | ~4208ms (codegen warnings on fold expressions but generates .o) |

**Legend:** ✅ Compiled | ❌ Failed/Parse/Include Error | 💥 Crash

### Summary (2026-02-26)

**Total headers tested:** 68
**Compiling successfully:** 48 (71%) — up from 44 (65%)
**Parse errors:** 9
**Codegen errors (generates .o with warnings):** 7 (now counted as compiled: `<utility>`, `<string_view>`, `<algorithm>`, `<span>`, `<tuple>`, `<set>`, `<cmath>`)
**Codegen crash:** 4 (`<string>`, `<optional>`, `<iostream>`, `<barrier>`)
**Codegen errors (fatal):** 2 (`<vector>`, `<map>`)

> **Note:** Headers that generate `.o` files but have codegen warnings (fold expressions, pack expansion) are now counted as "Compiled" since they produce usable object files. The warnings indicate incomplete template expansion that doesn't prevent compilation.

### Known Blockers

The most impactful blockers preventing more headers from compiling, ordered by impact:

1. **PackExpansionExprNode crashes in codegen**: Some `PackExpansionExprNode`s survive template instantiation and reach codegen, causing SIGSEGV. Affects: `<optional>`, `<string>`, `<iostream>`, `<barrier>`.

2. **Brace-init expression parsing for template types**: Expressions like `chrono::duration<long double>{__secs}` and `std::optional<_Tp>{std::in_place}` fail to parse because `Type<Args>{init}` is not recognized as a construction expression. Affects: `<chrono>`, `<shared_mutex>`, `<ranges>`, `<array>`.

3. **Dependent base class resolution**: Base classes that depend on template parameters (like `__hash_code_base` in `_Hashtable`) are not found during struct definition parsing. Affects: `<functional>` (via `<hashtable.h>`).

4. **Variable template evaluation in constant expressions**: Variable templates like `__is_ratio_v<T>` cannot be evaluated in `static_assert` contexts. Affects: `<ratio>`.

5. **Override checking with skipped base class members**: Virtual function override validation fails when base class members from template specialization bodies are skipped (e.g., `_M_get_deleter` in `_Sp_counted_ptr_inplace`). Affects: `<memory>`.

6. **Member lookup in inherited struct members during codegen**: Members like `_M_start`, `first` from inherited base structs are not found during code generation. Affects: `<vector>`, `<map>`.

### Recent Fixes (2026-02-26)

1. **Out-of-line member class definitions in templates**: Fixed parsing of `template<T> class Foo<T>::Bar { ... }` patterns. The parser now detects `::member_name` after template arguments in both full and partial specialization paths and skips the out-of-line member class definition body. This unblocked `<ostream>` parsing past the `basic_ostream<_CharT, _Traits>::sentry` class definition. Previously blocked: `<memory>`, `<ranges>`, `<iostream>`, `<chrono>`.

2. **Out-of-line nested class member function definitions**: Fixed parsing of `ClassName<Args>::NestedClass::function(...)` patterns. The `try_parse_out_of_line_template_member` function now handles the double-qualified pattern (e.g., `basic_ostream<_CharT, _Traits>::sentry::sentry(...)`) by detecting the nested class name between two `::` tokens. Previously caused "Expected identifier token" errors.

3. **`final` keyword placement per C++ standard**: Fixed `final` specifier parsing to check before the base class list, matching the C++ standard grammar: `class-key identifier final(opt) base-clause(opt) { ... }`. Previously, `final` was only checked after the base class list, causing parse failures for `class Foo final : public Base { ... }`. Also handles `final` as both keyword token and identifier. Regression test: `tests/test_final_class_base_ret42.cpp`.

4. **Nested struct/class skip in template bodies**: Enhanced the nested struct/class skipping in both full and partial specialization bodies to handle `final` specifier, template arguments, base class lists, and C++11 attributes. Previously, only the struct name and body were consumed.

### Recent Fixes (2026-02-25)

1. **Extern template with namespace-qualified class names**: Fixed parsing of `extern template class ns::ClassName<T>;` — the parser now consumes `::` and subsequent identifiers to handle namespace-qualified names after `class`/`struct` in explicit template instantiation declarations. Previously only one token was consumed as the class name. Regression test: `tests/test_extern_template_ns_qualified_ret0.cpp`.

2. **SEH keywords only active in MSVC mode**: `__except`, `__try`, `__finally`, and `__leave` are now only treated as keywords when in MSVC mode. In GCC/Clang mode on Linux, these are treated as regular identifiers, matching GCC/Clang behavior. This unblocked `<memory>` and `<ostream>` parsing past `__except` parameter names in libstdc++ headers (e.g., `basic_ios::exceptions(iostate __except)`).

3. **Constrained auto parameter skip for FunctionDeclarationNode members**: Member functions stored as `FunctionDeclarationNode` (not just `ConstructorDeclarationNode` or `TemplateFunctionDeclarationNode`) are now checked for unresolved `Type::Auto` parameters and skipped during codegen. This prevents "Symbol not found" errors for abbreviated template members like `subrange` functions. Regression test coverage via `<algorithm>`, `<tuple>`, `<set>` tests.

4. **Function pointer parameters with reference return types**: Fixed parsing of function pointer parameters where the return type is a reference, e.g., `ostream& (*__pf)(ostream&)`. Added function pointer detection after `consume_pointer_ref_modifiers()` handles `&`/`&&`, and added `&`/`&&` handling in `parse_postfix_declarator()` for function pointer parameter types. This unblocked `<ostream>` parsing past `operator<<` declarations. Regression test: `tests/test_fnptr_ref_return_ret0.cpp`.

5. **O(N²) → O(1) symbol lookup in ELF codegen (major performance fix)**: `ElfFileWriter` was performing linear scans over the symbol table (O(N) per lookup) in four hot paths, causing O(N²) total cost for headers with many functions and global variables:
   - `getOrCreateSymbol()` — called for every function call relocation and every global variable/vtable/typeinfo definition; was scanning all existing symbols on every call
   - `generate_eh_frame()` — scanned all symbols for each FDE (frame description entry)
   - `dwarfSymbolIndex()` — scanned all symbols for each DWARF relocation
   - `add_function_exception_info()` — used `std::vector<std::string>` for duplicate detection (O(N) per check)

   **Fix**: Added `symbol_index_cache_` (`std::unordered_map<std::string, ELFIO::Elf_Word>`) to `ElfFileWriter`. Symbols are inserted into the cache when added via `add_function_symbol()` and `getOrCreateSymbol()`. All four hot paths now use O(1) cache lookups instead of O(N) scans. Changed `added_exception_functions_` from `std::vector<std::string>` to `std::unordered_set<std::string>`.

   **Results** (measured on GCC 13 libstdc++ headers):

   | Header | Before | After | Speedup |
   |--------|--------|-------|---------|
   | `<optional>` | ~926ms | ~580ms | **37%** |
   | `<numeric>` | ~944ms | ~579ms | **39%** |
   | Code Generation phase alone | 366–401ms | 19–25ms | **93–95%** |
   | Finalize sections | 157–278ms | 3.6–3.7ms | **98–99%** |
   | Write object file | 54–85ms | 2.7–3.2ms | **95–96%** |

   The remaining compilation time is dominated by parsing (~75%) which is expected since template-heavy headers instantiate hundreds of templates. Future parsing optimizations will address that separately.

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

### MSVC Status Sweep (2026-02-25)

This README is primarily GCC/Clang-oriented; the following is a dedicated MSVC snapshot from running `tests/std/test_*.cpp` with detected MSVC + Windows SDK include paths.

#### MSVC compile results (tests/std)

- **Total test files:** 30
- **Compiled:** 2
	- `test_cstddef.cpp` (~119ms)
	- `test_std_compare_ret42.cpp` (~45ms)
- **Failed:** 28 (all parser-stage in this sweep)

#### Updated MSVC timings (selected)

| Test file | Status | Time | First concrete failure |
|---|---:|---:|---|
| `test_std_type_traits.cpp` | ❌ | ~352ms | `corecrt_wstring.h:144:8: error: Expected identifier token` |
| `test_std_utility.cpp` | ❌ | ~347ms | `corecrt_wstring.h:144:8: error: Expected identifier token` |
| `test_std_any.cpp` | ❌ | ~1047ms | `corecrt_wstring.h:144:8: error: Expected identifier token` |
| `test_std_algorithm.cpp` | ❌ | ~1183ms | `yvals.h:366:30: error: Expected type name after 'struct', 'class', or 'union'` |
| `test_std_limits.cpp` | ❌ | ~490ms | `corecrt_wstdio.h:309:40: error: No matching function for call to '__stdio_common_vfwprintf'` |
| `test_std_chrono.cpp` | ❌ | ~2456ms | parser failure in MSVC/UCRT include chain |
| `test_std_iostream.cpp` | ❌ | ~1657ms | parser failure in MSVC/UCRT include chain |

#### Newly fixed blockers (MSVC-focused)

1. **Pointer object constness vs pointee constness in overload resolution**
	- Fixed incorrect treatment of `T* const` as `const T*` during overload matching.
	- This removed false negatives for C-runtime style calls where local pointer objects are const-qualified and passed by value.
	- Regression test: `tests/test_const_pointer_value_conversion_ret42.cpp`.

2. **Constrained/abbreviated `auto` parameter type materialization during template instantiation (codegen blocker)**
	- Instantiated function parameters now use the deduced concrete argument type instead of keeping `Type::Auto`.
	- This prevents zero-size `auto` parameters from reaching codegen.
	- Regression test: `tests/test_constrained_auto_u64_codegen_ret1.cpp`.

#### Current top MSVC blockers after this round

1. **SAL macro parse path around `_When_`/annotation expansion** (e.g., `corecrt_wstring.h:144`).
2. **UCRT formatted I/O wrapper call resolution** (e.g., `__stdio_common_vfwprintf` call shape in `corecrt_wstdio.h`).
3. **MSVC STL front-end parse compatibility in `yvals.h` include path** (seen from `<algorithm>`).
