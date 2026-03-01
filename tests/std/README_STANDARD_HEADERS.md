# Standard Header Tests

This directory contains test files for C++ standard library headers to assess FlashCpp's compatibility with the C++ standard library.

## Current Status

| Header | Test File | Status | Notes |
|--------|-----------|--------|-------|
| `<limits>` | `test_std_limits.cpp` | ✅ Compiled | ~119ms |
| `<type_traits>` | `test_std_type_traits.cpp` | ✅ Compiled | ~184ms |
| `<compare>` | `test_std_compare_ret42.cpp` | ✅ Compiled | ~10ms |
| `<version>` | `test_std_version.cpp` | ✅ Compiled | ~22ms |
| `<source_location>` | `test_std_source_location.cpp` | ✅ Compiled | ~22ms |
| `<numbers>` | N/A | ✅ Compiled | ~194ms |
| `<initializer_list>` | N/A | ✅ Compiled | ~15ms |
| `<ratio>` | `test_std_ratio.cpp` | ✅ Compiled | ~277ms. ratio_equal works; ratio_less needs default parameter evaluation |
| `<optional>` | `test_std_optional.cpp` | ✅ Compiled | ~597ms |
| `<any>` | `test_std_any.cpp` | ✅ Compiled | ~274ms |
| `<utility>` | `test_std_utility.cpp` | ✅ Compiled | ~362ms |
| `<concepts>` | `test_std_concepts.cpp` | ✅ Compiled | ~215ms |
| `<bit>` | `test_std_bit.cpp` | ✅ Compiled | ~261ms |
| `<string_view>` | `test_std_string_view.cpp` | ✅ Compiled | ~1207ms |
| `<string>` | `test_std_string.cpp` | ✅ Compiled | ~2095ms |
| `<array>` | `test_std_array.cpp` | ❌ Parse Error | Aggregate brace initialization not supported for template types (`std::array<int,5> arr = {1,2,3,4,5}`) |
| `<algorithm>` | `test_std_algorithm.cpp` | ✅ Compiled | ~1362ms |
| `<span>` | `test_std_span.cpp` | ✅ Compiled | ~937ms |
| `<tuple>` | `test_std_tuple.cpp` | ✅ Compiled | ~901ms |
| `<vector>` | `test_std_vector.cpp` | ✅ Compiled | ~1750ms |
| `<deque>` | `test_std_deque.cpp` | ✅ Compiled | ~1520ms |
| `<list>` | `test_std_list.cpp` | ✅ Compiled | ~1358ms |
| `<queue>` | `test_std_queue.cpp` | ✅ Compiled | ~1921ms |
| `<stack>` | `test_std_stack.cpp` | ✅ Compiled | ~1528ms |
| `<memory>` | `test_std_memory.cpp` | ❌ Crash/Timeout | Stack overflow during template instantiation (6200+ templates) |
| `<functional>` | `test_std_functional.cpp` | ❌ Parse Error | Base class `__hash_code_base` not found in `<hashtable.h>` (dependent base class) |
| `<map>` | `test_std_map.cpp` | ✅ Compiled | ~1808ms |
| `<set>` | `test_std_set.cpp` | ✅ Compiled | ~1538ms |
| `<ranges>` | `test_std_ranges.cpp` | ❌ Parse Error | Ambiguous call to `__to_unsigned_like` |
| `<iostream>` | `test_std_iostream.cpp` | ✅ Compiled | ~3139ms |
| `<sstream>` | `test_std_sstream.cpp` | ✅ Compiled | ~3267ms |
| `<fstream>` | `test_std_fstream.cpp` | ✅ Compiled | ~3374ms |
| `<chrono>` | `test_std_chrono.cpp` | 💥 Crash | Stack overflow during template instantiation (7500+ templates); previously parse error on brace-init |
| `<atomic>` | `test_std_atomic.cpp` | ✅ Compiled | ~1081ms |
| `<new>` | `test_std_new.cpp` | ✅ Compiled | ~33ms |
| `<exception>` | `test_std_exception.cpp` | ✅ Compiled | ~247ms |
| `<stdexcept>` | `test_std_stdexcept.cpp` | ✅ Compiled | ~2129ms |
| `<typeinfo>` | N/A | ✅ Compiled | ~32ms |
| `<typeindex>` | N/A | ✅ Compiled | ~284ms |
| `<numeric>` | `test_std_numeric.cpp` | ✅ Compiled | ~566ms |
| `<iterator>` | `test_std_iterator.cpp` | ✅ Compiled | ~2614ms |
| `<variant>` | `test_std_variant.cpp` | ❌ Parse Error | Expected ';' after struct/class definition at variant:1137 |
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
| `<latch>` | `test_std_latch.cpp` | ✅ Compiled | ~650ms |
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
| `<cmath>` | `test_std_cmath.cpp` | ✅ Compiled | ~3999ms |

**Legend:** ✅ Compiled | ❌ Failed/Parse/Include Error | 💥 Crash

### Summary (2026-03-01)

**Total headers tested:** 77
**Compiling successfully:** 66 (86%) — up from 65 (84%) on 2026-03-01 (ratio fixed)
**Parse errors:** 8
**Codegen crashes/fatal errors:** 3 (barrier, memory, chrono — all stack overflow during deep template instantiation)
**Headers newly compiling this session:** `<deque>`, `<list>`, `<queue>`, `<stack>`, `<sstream>`, `<fstream>`, `<iterator>`, `<stdexcept>`, `<numeric>` (test files), plus many existing headers now produce .o files reliably

> **Note:** All 66 "compiled" headers generate `.o` object files. Some have codegen warnings about fold expressions, pack expansions, or unsupported arithmetic types that weren't fully handled, but these don't prevent compilation — individual function code generation errors are now non-fatal and the failing functions are skipped.

### Known Blockers

The most impactful blockers preventing more headers from compiling, ordered by impact:

1. **Brace-init expression parsing for template types**: Expressions like `chrono::duration<long double>{__secs}` and `std::optional<_Tp>{std::in_place}` fail to parse because `Type<Args>{init}` is not recognized as a construction expression. Affects: `<shared_mutex>`, `<ranges>`.

2. **Aggregate brace initialization for template types**: `std::array<int, 5> arr = {1, 2, 3, 4, 5}` fails because FlashCpp treats `{}` as constructor lookup rather than aggregate initialization. Affects: `<array>`.

3. **Dependent base class resolution**: Base classes that depend on template parameters (like `__hash_code_base` in `_Hashtable`) are not found during struct definition parsing. Affects: `<functional>` (via `<hashtable.h>`), `<unordered_set>`, `<unordered_map>`.

4. **Variant struct/class definition parsing**: The `<variant>` header has a complex struct definition with pack expansion in base class lists that fails with "Expected ';' after struct/class definition" at variant:1137. Affects: `<variant>`.

5. **Stack overflow during deep template instantiation**: Headers like `<memory>`, `<barrier>`, and `<chrono>` trigger 6000-7500+ template instantiations that exhaust the stack. Affects: `<memory>`, `<barrier>`, `<chrono>`.

### Recent Fixes (2026-03-01, session 2)

1. **`<ratio>` header now compiles (~277ms)**: Fixed a chain of 5 issues preventing `std::ratio`, `std::ratio_equal`, and the internal `__static_sign`/`__static_abs`/`__static_gcd` helpers from working:

   a. **Lazy static member instantiation in constexpr evaluator**: When evaluating `ratio$hash::num`, the evaluator now always triggers lazy instantiation before evaluating the initializer, ensuring template parameter substitution runs first.

   b. **Binary operator `ctx.parser` in `try_evaluate_constant_expression`**: The `BinaryOperatorNode` handler was missing `ctx.parser = this`, preventing lazy template instantiation during binary expression evaluation (e.g., `_R1::num == _R2::num`).

   c. **Lazy member constant folding**: After lazy static member substitution, the initializer expression is now evaluated to a constant `NumericLiteralNode` when possible, enabling downstream constexpr chains.

   d. **TernaryOperatorNode support in ExpressionSubstitutor**: Added `substituteTernaryOp()` method to recursively substitute template parameters in ternary expressions like `(_Pn < 0) ? -1 : 1` used in `__static_sign`'s deferred base class. Also added `ctx.parser` and struct context to the ternary handler in `try_evaluate_constant_expression`.

   e. **ExpressionSubstitutor template arg extraction from TypeInfo**: Instead of blindly using the outer template's full `param_map_` (e.g., all params of `ratio<_Num, _Den>`), the substitutor now looks up the placeholder TypeInfo's stored template args. This correctly maps `__static_sign<_Den>` to 1 argument (not 2), using the `dependent_name` field to substitute the right parameter.

2. **`<chrono>` gets further (7500+ templates)**: The fixes above allowed `<chrono>` to parse past its previous brace-init error, but it now hits stack overflow during deep template instantiation (similar to `<memory>` and `<barrier>`).

### Recent Fixes (2026-03-01)

1. **Per-function error recovery in IrToObjConverter**: The code generation phase (IR → machine code) now catches exceptions per-function and skips failed functions instead of aborting the entire compilation. When a function's code generation fails (e.g., unsupported arithmetic type, unresolved label), the function is skipped and compilation continues with the next function. This produces valid .o files for all successfully generated functions.

2. **IR conversion errors made non-fatal**: Previously, any IR conversion error (even in a single template function) would prevent .o file generation entirely. Now, individual function failures during IR conversion and deferred member function generation are logged as warnings, and the .o file is still produced. This recovered many std headers that were blocking on template edge cases.

3. **Per-function try-catch in generateDeferredMemberFunctions**: Deferred member function generation now catches and logs exceptions per-function instead of propagating the first error as fatal. Returns a count of failed functions instead of void.

4. **SIGSEGV-safe member access codegen**: All error paths in `generateMemberAccessIr` now throw `std::runtime_error` instead of returning empty vectors. The empty vector returns caused downstream SIGSEGV when callers tried to index into them. The thrown exceptions are caught by the per-function error recovery mechanism. This fixed crashes in `<optional>`, `<map>`, and `<memory>`.

5. **Register allocator assertions converted to exceptions**: Critical asserts in `RegisterAllocator::set_stack_variable_offset`, `allocateSpecific`, `mark_reg_dirty`, and `flushSingleDirtyRegister` were converted to `throw std::runtime_error`. This allows the per-function error recovery to catch register allocation failures instead of crashing the compiler with SIGABRT.

6. **handleFloatToInt assertion converted to exception**: The assert in `handleFloatToInt` that checked for `StringHandle` variant type is now a throw, allowing recovery from unexpected operand types during code generation.

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
