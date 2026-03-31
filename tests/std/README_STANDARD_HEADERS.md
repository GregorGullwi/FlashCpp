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
| `<ratio>` | `test_std_ratio.cpp` | ✅ Compiled | ~281ms. ratio_equal works; ratio_less needs default parameter evaluation |
| `<optional>` | `test_std_optional.cpp` | ❌ Parse Error | Unexpanded pack expansion |
| `<any>` | `test_std_any.cpp` | ✅ Compiled | ~271ms |
| `<utility>` | `test_std_utility.cpp` | ✅ Compiled | ~356ms |
| `<concepts>` | `test_std_concepts.cpp` | ✅ Compiled | ~220ms |
| `<bit>` | `test_std_bit.cpp` | ✅ Compiled | ~237ms |
| `<string_view>` | `test_std_string_view.cpp` | ❌ Codegen Error | Operator== not defined for operand types |
| `<string>` | `test_std_string.cpp` | ❌ Compile Error | ~4150ms (targeted retest 2026-03-31). Previous function-pointer mangling blocker fixed; now fails later on `Missing identifier: string`, missing `std::basic_string` primary template lookups, and explicit-constructor copy-init errors |
| `<array>` | `test_std_array.cpp` | ❌ Parse Error | Aggregate brace initialization not supported for template types |
| `<algorithm>` | `test_std_algorithm.cpp` | ❌ Compile Error | ~2100ms (targeted retest 2026-03-31). No longer reproduces unresolved-`auto` mangling here; now fails later on explicit-constructor copy-init after concepts/ranges diagnostics |
| `<span>` | `test_std_span.cpp` | ❌ Parse Error | |
| `<tuple>` | `test_std_tuple.cpp` | ❌ Codegen Error | Itanium mangling: unresolved 'auto' type reached mangling |
| `<vector>` | `test_std_vector.cpp` | ❌ Codegen Error | member '_M_start' not found in struct '_Vector_impl' (base class member access) |
| `<deque>` | `test_std_deque.cpp` | ❌ Codegen Error | Itanium mangling: unresolved 'auto' type reached mangling |
| `<list>` | `test_std_list.cpp` | ❌ Codegen Error | member '_M_impl' not found in struct 'std::__cxx11::list' |
| `<queue>` | `test_std_queue.cpp` | ❌ Codegen Error | Itanium mangling: unresolved 'auto' type reached mangling |
| `<stack>` | `test_std_stack.cpp` | ❌ Codegen Error | Itanium mangling: unresolved 'auto' type reached mangling |
| `<memory>` | `test_std_memory.cpp` | ❌ Codegen Error | Itanium mangling: unresolved 'auto' type reached mangling |
| `<functional>` | `test_std_functional.cpp` | ❌ Codegen Error | Itanium mangling: unresolved 'auto' type reached mangling |
| `<map>` | `test_std_map.cpp` | ❌ Codegen Error | member 'first' not found in struct 'std::iterator' |
| `<set>` | `test_std_set.cpp` | ❌ Codegen Error | Itanium mangling: unresolved 'auto' type reached mangling |
| `<ranges>` | `test_std_ranges.cpp` | ❌ Parse Error | Ambiguous call to `__to_unsigned_like` |
| `<iostream>` | `test_std_iostream.cpp` | ❌ Codegen Error | char_traits member functions not found during deferred body codegen |
| `<sstream>` | `test_std_sstream.cpp` | ❌ Codegen Error | char_traits member functions not found during deferred body codegen |
| `<fstream>` | `test_std_fstream.cpp` | ❌ Codegen Error | char_traits member functions not found during deferred body codegen |
| `<chrono>` | `test_std_chrono.cpp` | 💥 Crash | Stack overflow during template instantiation (7500+ templates) |
| `<atomic>` | `test_std_atomic.cpp` | ❌ Parse Error | non-dependent name `__w` not declared before template definition |
| `<new>` | `test_std_new.cpp` | ✅ Compiled | ~24ms |
| `<exception>` | `test_std_exception.cpp` | ✅ Compiled | ~238ms |
| `<stdexcept>` | `test_std_stdexcept.cpp` | ❌ Compile Error | ~4220ms (targeted retest 2026-03-31). Same post-fix state as `<string>`: no function-pointer mangling crash, but later `string`/`std::basic_string` and explicit-constructor copy-init failures remain |
| `<typeinfo>` | N/A | ✅ Compiled | ~32ms |
| `<typeindex>` | N/A | ✅ Compiled | ~284ms |
| `<numeric>` | `test_std_numeric.cpp` | ✅ Compiled | ~632ms |
| `<iterator>` | `test_std_iterator.cpp` | ✅ Compiled | ~1669ms (some codegen warnings) |
| `<variant>` | `test_std_variant.cpp` | ❌ Parse Error | Parser fails to close `_Copy_assign_base` struct body |
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
| `<latch>` | `test_std_latch.cpp` | ❌ Parse Error | non-dependent name `__w` not declared before template definition |
| `<shared_mutex>` | N/A | ❌ Parse Error | Fails on `chrono::duration<long double>{__secs}` brace-init expression |
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
**Crashes:** 4 (barrier, chrono, condition_variable — all stack overflow during deep template instantiation)

**Targeted retest note (2026-03-31):** The table entries for `<compare>`, `<algorithm>`, `<string>`, and `<stdexcept>` were re-verified individually after the function-pointer signature fix below. The overall counts above still reflect the older full sweep and need a future comprehensive rerun before they are updated.

### Known Blockers

The most impactful blockers preventing more headers from compiling, ordered by impact:

1. **Template deduction / semantic follow-on failures after the earlier mangling blockers**: In the 2026-03-31 targeted retest, `<algorithm>` no longer failed first on unresolved-`auto` mangling. It now gets further and then fails on concepts/ranges diagnostics followed by explicit-constructor copy-initialization errors. The same family of deeper issues likely explains several headers previously bucketed under the stale unresolved-`auto` note.

2. **Function pointer signature propagation through template instantiation metadata (fixed 2026-03-31)**: Function pointer signatures were being dropped in lazy member instantiation, outer-template bindings, and free-function template instantiation. This previously caused the Itanium mangler to crash on headers such as `<string>` and `<stdexcept>`. After the fix, those headers progress past the mangling crash and now fail later on unrelated issues.

   - Core fix areas: `Parser_Templates_Lazy.cpp`, `Parser_Templates_Inst_Deduction.cpp`, and `TypeInfo::TemplateArgInfo` / outer-template binding serialization.
   - Regression test: `tests/test_funcptr_lazy_member_signature_ret0.cpp`.

3. **Brace-init expression parsing for template types**: Expressions like `chrono::duration<long double>{__secs}` and `std::optional<_Tp>{std::in_place}` fail to parse because `Type<Args>{init}` is not recognized as a construction expression. Affects: `<shared_mutex>`, `<ranges>`.

4. **Aggregate brace initialization for template types**: `std::array<int, 5> arr = {1, 2, 3, 4, 5}` fails because the struct member `_M_elems` (from `typename __array_traits<_Tp,_Nm>::_Type`) is not resolved as an array type, so brace-elision doesn't apply. Affects: `<array>`.

5. **Variant struct/class definition parsing**: The `<variant>` header's `_Copy_assign_base` class has complex lambda with `if constexpr` inside `operator=` that prevents the parser from properly closing the struct body. Affects: `<variant>`.

6. **Ambiguous overload resolution**: `__to_unsigned_like` in ranges has multiple overloads that the overload resolver treats as ambiguous. Affects: `<ranges>`.

7. **Stack overflow during deep template instantiation**: Headers like `<barrier>`, and `<chrono>` trigger 6000-7500+ template instantiations that exhaust the stack. Affects: `<barrier>`, `<chrono>`, `<condition_variable>`.

8. **Base class member access in codegen**: Generated code fails to find members inherited from base classes (e.g., `_M_start` in `_Vector_impl`, `_M_impl` in `list`, `first` in `iterator`). Affects: `<vector>`, `<list>`, `<map>`.

9. **Static sibling member function calls in template deferred bodies**: In deferred template member function bodies, calls to other member functions of the same class fail because sibling functions are not registered in the codegen symbol table context. Affects: `<iostream>`, `<sstream>`, `<fstream>`.

### Recent Fixes (2026-03-31)

1. **Function pointer signatures now survive template instantiation and deferred/lazy member materialization**: Several template paths rebuilt `TemplateTypeArg` or `TypeSpecifierNode` values from bare `TypeIndex` metadata, silently discarding `function_signature`. This broke Itanium mangling for instantiated function-pointer parameters such as `__gnu_cxx::__stoa(_TRet (*__convf)(...), ...)` from `<string>`. The fix preserves `function_signature` in lazy member instantiation, free-function template instantiation, and the metadata used for outer-template bindings / stored template args. Regression tests: existing `tests/test_funcptr_template_signature_ret0.cpp` plus new `tests/test_funcptr_lazy_member_signature_ret0.cpp`.

2. **`<compare>` targeted retest now compiles again**: `tests/std/test_std_compare_ret42.cpp` currently compiles in ~20ms on Linux with the current branch state. The older `weak_ordering` constructor note was stale.

### Recent Fixes (2026-03-14)

1. **ExpressionSubstitutor: StaticCastNode handling**: Functional-style casts like `bool(P::value)` are parsed as `StaticCastNode` but the `ExpressionSubstitutor` didn't handle this variant type. Added `substituteStaticCast()` which recursively substitutes the inner expression and target type. This was the primary blocker for `__not_<X>::type` resolution used throughout `<type_traits>`. Regression tests: `tests/test_template_not_bool_cast_ret0.cpp`.

2. **Dependent template placeholder detection in base class arguments**: When a template argument like `is_fundamental<T>` (where `T` is a template parameter) creates a hash-based dependent placeholder (e.g., `is_fundamental$00a3b7d5205981a7`), the base class dependency detection failed to recognize it as dependent because the hashed name doesn't contain the literal parameter name `T`. Added `isDependentTemplatePlaceholder()` check alongside the existing string-based heuristic. Regression test: `tests/test_template_dependent_placeholder_base_ret0.cpp`.

3. **Template type parameter functional-style cast disambiguation**: Statements like `_Tp(args...).swap(c)` (from `alloc_traits.h:908`) failed with "Expected identifier token" because the parser treated `_Tp(...)` as a variable declaration when `_Tp` is a template type parameter. Added look-ahead in the template parameter check in `parse_statement_or_declaration()` to detect `.` or `->` after the closing `)`, routing to expression parsing instead. Uses `peek(1)` to look past the current identifier token. Regression test: `tests/test_template_type_param_functional_cast_ret0.cpp`.

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

### Recent Fixes (2026-03-03)

1. **Template friend class declarations now register correctly**: `template<typename T> friend struct Foo;` declarations are parsed by `parse_template_friend_declaration` into `FriendDeclarationNode(FriendKind::TemplateClass, ...)`. Previously these were NOT registered into `StructTypeInfo::friend_classes_` — the `FriendKind::TemplateClass` case was missing from the registration switch in `parse_struct_definition`, and the result from `parse_member_template_or_function` was silently dropped. Now both the `template` keyword branch in `parse_struct_definition` and the `parse_friend_declaration` path handle `FriendKind::TemplateClass` registrations. This fixed the `__use_cache` access control violation that was the primary blocker for `<iostream>`, `<sstream>`, and `<fstream>`. Regression test: `tests/test_friend_template_class_ret0.cpp`.

2. **`isFriendClass` handles template instantiation names**: The `isFriendClass` method in `StructTypeInfo` now also strips FlashCpp's internal `_pattern_` suffix and `$hash` suffixes from the checking name before looking up in `friend_classes_`. This ensures that any instantiated template (e.g., `__use_cache$hash`) is recognized as a friend when the base template name (`__use_cache`) was declared as a friend.

3. **`checkMemberAccess` / `checkMemberFunctionAccess` try unqualified names**: When the accessing struct's name is namespace-qualified (e.g., `std::__use_cache_pattern_`), the access check now also tries the unqualified tail of the name (e.g., `__use_cache_pattern_`) for friend class matching.

### Recent Fixes (2026-03-01, session 3)

1. **Pack expansion `...` in template base class lists**: The parser now correctly handles `Base<Args>...` in base class lists (e.g., `struct Derived : Base<Indices, Type>...`). The `...` after template base classes is consumed and stored as `is_pack_expansion` in `DeferredTemplateBaseClassSpecifier`. This was the primary blocker for `<variant>`'s `_Variant_hash_base` partial specialization. Regression test: `tests/test_pack_expansion_base_class_ret0.cpp`.

2. **Deferred unresolved base class resolution in template bodies**: When parsing a template body, base class names that are not found in the global type registry (e.g., member type aliases like `__hash_code_base` in `_Hashtable`) are now deferred instead of producing an error. The base class is added with `is_deferred=true` and will be resolved during template instantiation. Previously unblocked: `<functional>` past `__hash_code_base`, `<unordered_set>`, `<unordered_map>`. Regression test: `tests/test_deferred_base_class_template_ret0.cpp`.

3. **Parenthesized `>` in template parameter defaults**: Inside parenthesized expressions within template argument context, `>` is now correctly treated as the greater-than operator rather than a template closing bracket. This fixes patterns like `bool _IsPlaceholder = (is_placeholder<_Arg>::value > 0)` from `<functional>`. The fix changes the `ExpressionContext` to `Normal` when entering parenthesized subexpressions from `TemplateArgument` context. Regression test: `tests/test_paren_gt_template_default_ret0.cpp`.

4. **Trailing `volatile&` / `const volatile&` type parsing**: The `consume_pointer_ref_modifiers` function now handles CV-qualifiers (const, volatile) that appear between the type name and a reference declarator (e.g., `Type volatile&`). Uses lookahead to ensure CV-qualifiers are only consumed when actually followed by `&` or `&&`, preventing false positives in other contexts. Previously failed on `__volget`'s return type `__tuple_element_t<...> volatile&` in `<functional>`. Regression test: `tests/test_volatile_ref_type_ret0.cpp`.

5. **Updated status of previously-documented headers**: Investigation confirmed that `<vector>`, `<list>`, `<map>`, `<iostream>`, `<sstream>`, and `<fstream>` have codegen errors (not parsing errors) that were pre-existing before this session. The README has been updated to accurately reflect their current status with specific error messages.

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
