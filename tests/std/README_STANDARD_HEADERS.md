# Standard Header Tests

This directory contains test files for C++ standard library headers to assess FlashCpp's compatibility with the C++ standard library.

## Current Status

| Header | Test File | Status | Notes |
|--------|-----------|--------|-------|
| `<limits>` | `test_std_limits.cpp` | ✅ Compiled | ~29ms |
| `<type_traits>` | `test_std_type_traits.cpp` | ✅ Compiled | ~187ms; unary trait constants synthesized (2026-02-04, emits zero-init logs for integral_constant::value) |
| `<compare>` | N/A | ✅ Compiled | ~258ms (2026-01-24: Fixed with operator[], brace-init, and throw expression fixes) |
| `<version>` | N/A | ✅ Compiled | ~17ms |
| `<source_location>` | N/A | ✅ Compiled | ~17ms |
| `<numbers>` | N/A | ✅ Compiled | ~33ms |
| `<initializer_list>` | N/A | ✅ Compiled | ~16ms |
| `<ratio>` | `test_std_ratio.cpp` | ❌ Parse Error | Template lookup failure for `__ratio_add_impl` (non-type bool default params); memory corruption fixed |
| `<vector>` | `test_std_vector.cpp` | ❌ Parse Error | Progressed past base class resolution; now fails at `stl_vector.h:608` (ambiguous overloaded function `_M_get_Tp_allocator`) |
| `<tuple>` | `test_std_tuple.cpp` | ❌ Parse Error | Progressed past `_Elements` pack error; now fails at `tuple:2877` (out-of-line pair ctor with `sizeof...` in dependent `typename` member init) |
| `<optional>` | `test_std_optional.cpp` | ✅ Compiled | ~759ms (2026-02-08: Fixed with ref-qualifier, explicit constexpr, and attribute fixes) |
| `<variant>` | `test_std_variant.cpp` | ❌ Parse Error | Progressed past body parsing; now fails at `variant:831` (member variable template with concept constraint) |
| `<any>` | `test_std_any.cpp` | ✅ Compiled | ~300ms (previously blocked by out-of-line template member) |
| `<concepts>` | `test_std_concepts.cpp` | ✅ Compiled | ~100ms |
| `<utility>` | `test_std_utility.cpp` | ✅ Compiled | ~311ms (2026-01-30: Fixed with dependent template instantiation fix) |
| `<bit>` | N/A | ✅ Compiled | ~80ms (2026-02-06: Fixed with `__attribute__` and type trait whitelist fixes) |
| `<string_view>` | `test_std_string_view.cpp` | ❌ Codegen Error | Parsing completes; fails during IR conversion (`bad_any_cast` in template member body) |
| `<string>` | `test_std_string.cpp` | ❌ Codegen Error | Parsing completes; fails during IR conversion (`bad_any_cast` in template member body) |
| `<array>` | `test_std_array.cpp` | ✅ Compiled | ~738ms (2026-02-08: Fixed with deduction guide and namespace-qualified call fixes) |
| `<memory>` | `test_std_memory.cpp` | ❌ Parse Error | Progressed past `_Elements` pack; now fails at `tuple:2877` (same as tuple) |
| `<functional>` | `test_std_functional.cpp` | ❌ Parse Error | Progressed past `_Elements` pack; now fails at `tuple:2877` (same as tuple) |
| `<algorithm>` | `test_std_algorithm.cpp` | ❌ Parse Error | Now fails at `uniform_int_dist.h:289` (nested template `operator()` out-of-line definition) |
| `<map>` | `test_std_map.cpp` | ❌ Parse Error | Progressed past brace-init return; now blocked by later parse errors in `stl_tree.h` |
| `<set>` | `test_std_set.cpp` | ❌ Parse Error | Progressed past brace-init return; now blocked by later parse errors in `stl_tree.h` |
| `<span>` | `test_std_span.cpp` | ❌ Codegen Error | Parsing completes; crash during IR conversion (`assert` in `setupAndLoadArithmeticOperation`) |
| `<ranges>` | `test_std_ranges.cpp` | ❌ Codegen Error | Parsing completes; fails during IR conversion (`bad_any_cast` in template member body) |
| `<iostream>` | `test_std_iostream.cpp` | ❌ Codegen Error | Parsing completes; fails during IR conversion (`bad_any_cast` in template member body) |
| `<chrono>` | `test_std_chrono.cpp` | ✅ Compiled | ~287ms (2026-02-08: Fixed with ref-qualifier and attribute fixes) |
| `<atomic>` | N/A | ❌ Parse Error | Conversion operator in partial specialization fixed; now fails at `atomic_base.h:1562` (`compare_exchange_weak` template instantiation) |
| `<new>` | N/A | ✅ Compiled | ~18ms |
| `<exception>` | N/A | ✅ Compiled | ~43ms |
| `<typeinfo>` | N/A | ✅ Compiled | ~43ms (2026-02-05: Fixed with _Complex and __asm support) |
| `<typeindex>` | N/A | ✅ Compiled | ~43ms (2026-02-05: Fixed with _Complex and __asm support) |
| `<csetjmp>` | N/A | ✅ Compiled | ~16ms |
| `<csignal>` | N/A | ❌ Parse Error | `__attribute_deprecated_msg__` at `signal.h:368` (pre-existing, depends on system headers) |
| `<stdfloat>` | N/A | ✅ Compiled | ~14ms (C++23) |
| `<spanstream>` | N/A | ✅ Compiled | ~17ms (C++23) |
| `<print>` | N/A | ✅ Compiled | ~17ms (C++23) |
| `<expected>` | N/A | ✅ Compiled | ~18ms (C++23) |
| `<text_encoding>` | N/A | ✅ Compiled | ~17ms (C++26) |
| `<barrier>` | N/A | ❌ Parse Error | `__cmpexch_failure_order2` overload resolution at `atomic_base.h:128` (enum bitwise ops) |
| `<stacktrace>` | N/A | ✅ Compiled | ~17ms (C++23) |
| `<coroutine>` | N/A | ❌ Parse Error | Qualified static member brace init fixed; now hangs during template instantiation (infinite loop) |

**Legend:** ✅ Compiled | ❌ Failed/Parse/Include Error | ⏱️ Timeout (60s) | 💥 Crash

### Recent Fixes (2026-02-06)

The following parser issues were fixed to unblock standard header compilation:

1. **GCC `__attribute__((...))` between return type and function name**: `parse_type_and_name()` now skips `__attribute__` specifications that appear after the return type but before the function name (e.g., `_Atomic_word __attribute__((__always_inline__)) __exchange_and_add(...)`). This unblocks `atomicity.h` used by `<iostream>`.

2. **`__ATOMIC_*` memory ordering macros**: Added `__ATOMIC_RELAXED` (0), `__ATOMIC_CONSUME` (1), `__ATOMIC_ACQUIRE` (2), `__ATOMIC_RELEASE` (3), `__ATOMIC_ACQ_REL` (4), `__ATOMIC_SEQ_CST` (5) as predefined macros. These are used by `<atomic>` and `<iostream>` via `atomicity.h`.

3. **Type trait whitelist instead of prefix matching**: The expression parser now uses a whitelist of known type traits (`__is_void`, `__is_integral`, etc.) instead of matching all identifiers with `__is_*` or `__has_*` prefixes. This prevents regular functions like `__gnu_cxx::__is_single_threaded()` from being misidentified as type trait intrinsics. Unblocks `<iostream>`.

4. **Conversion operator detection in template struct bodies**: Template specialization struct body parsing now correctly detects conversion operators like `constexpr explicit operator bool() const noexcept`. Previously these failed with "Unexpected token in type specifier: 'operator'" because `parse_type_and_name()` doesn't handle conversion operators. This progresses `<coroutine>`.

5. **`sizeof` returning 0 for dependent types**: `evaluate_sizeof()` now returns a `TemplateDependentExpression` error instead of 0 when the type size is unknown. In standard C++, `sizeof` never returns 0, so a zero result indicates an incomplete or dependent type. This prevents false `static_assert` failures in templates.

6. **Improved `static_assert` deferral in template contexts**: `parse_static_assert()` now always defers evaluation when the condition contains template-dependent expressions (regardless of parsing context), and also defers in template struct bodies when evaluation fails for any reason. This unblocks `<atomic>` and `<barrier>` past the `static_assert(sizeof(__waiter_type) == sizeof(__waiter_pool_base))` check.

### Recent Fixes (2026-02-06, PR #2)

The following parser issues were fixed to unblock standard header compilation:

1. **`::operator new()` and `::operator delete()` in expressions and statements**: Added handling for `::operator new(...)`, `::operator delete(...)`, and array variants in both expression context (e.g., `static_cast<_Tp*>(::operator new(__n * sizeof(_Tp)))`) and statement context. This unblocks `new_allocator.h` which is included by all container headers.

2. **`alignas` with expression arguments**: `parse_alignas_specifier()` now falls back to parsing a constant expression when literal and type parsing fail. This handles patterns like `alignas(__alignof__(_Tp2::_M_t))` used in `aligned_buffer.h`. Unblocks `<variant>`.

3. **C-style cast backtracking**: When a C-style cast is attempted but the expression after the cast fails to parse (e.g., `(__p)` followed by `,`), the parser now backtracks instead of returning an error. This fixes function arguments like `::operator delete((__p), (__n) * sizeof(_Tp))`.

4. **`__attribute__` between `using` alias name and `=`**: `parse_member_type_alias()` now calls `skip_gcc_attributes()` after the alias name, handling patterns like `using is_always_equal __attribute__((__deprecated__("..."))) = true_type;`.

5. **Destructor in full template specializations**: Added destructor parsing (`~ClassName()`) in the full template specialization body loop, matching the existing handling in partial specializations.

6. **Template constructor detection in full specializations**: Constructor detection in `parse_member_function_template()` now also checks the base template name (via `TypeInfo::baseTemplateName()`), not just the instantiated name. This correctly detects `allocator(...)` as a constructor in `template<> struct allocator<void>`.

7. **Delayed function body processing for constructors/destructors**: Fixed null pointer dereference in full specialization delayed body processing when `func_node` is null (which it is for constructors/destructors). Now correctly handles constructor/destructor bodies and template parameter restoration.

8. **Template base class in member initializer lists**: Added `skip_template_arguments()` handling in ALL initializer list parsing locations (4 files) for patterns like `: Base<T>(args)`.

9. **`Base<T>::member(args)` qualified call expressions**: Statement parser now recognizes `Type<Args>::member(args)` as an expression (not a variable declaration) when `::` follows template arguments. Expression parser handles `::member(args)` after template argument disambiguation.

### Recent Fixes (2026-02-08)

The following parser issues were fixed to unblock standard header compilation:

1. **Member function pointer types in template specialization patterns**: Added parsing of `_Res (_Class::*)(_ArgTypes...)` syntax in template arguments, including variants with `const`, `volatile`, `&`, `&&`, `noexcept`, and C-style varargs (`_ArgTypes... ...`). This unblocks `refwrap.h` used by `<string>`, `<iostream>`, and 10+ other headers.

2. **Bare function types in template arguments**: Added parsing of `_Res(_ArgTypes...)` (bare function types, not pointers) in template specialization patterns. This handles `_Weak_result_type_impl<_Res(_ArgTypes...)>` patterns in `refwrap.h`.

3. **`noexcept(expr)` on function types in template arguments**: Extended function type parsing in template arguments to handle `noexcept(_NE)` conditional noexcept on function types, function pointers, and member function pointers.

4. **`= delete` / `= default` on static member functions**: Static member function declarations now accept `= delete;` and `= default;` syntax.

5. **Conversion operators returning reference types**: Conversion operator detection in struct bodies now handles pointer (`*`) and reference (`&`, `&&`) modifiers after the target type (e.g., `operator _Tp&() const noexcept`).

6. **Template context flags for `static_assert` deferral in member struct template bodies**: `parse_member_struct_template()` now properly sets `parsing_template_body_` and `current_template_param_names_` before parsing the body of both primary and partial specialization member struct templates. This ensures `static_assert` with template-dependent expressions is properly deferred. Unblocks `alloc_traits.h`.

7. **`decltype` expression fallback with pack expansion**: Fixed `parse_decltype_specifier()` to save and restore the parser position before the expression when the fallback to dependent type is triggered. Previously, failed expression parsing (e.g., `decltype(func(args...))` with pack expansion) could leave the parser at an unpredictable position, causing the paren-skipping logic to malfunction. Unblocks trailing return types with pack expansion in `alloc_traits.h`.

### Current Blockers for Major Headers

| Blocker | Affected Headers | Details |
|---------|-----------------|---------|
| IR conversion `bad_any_cast` during template member body | `<string_view>`, `<string>`, `<iostream>`, `<ranges>` | Parsing succeeds but codegen crashes on instantiated template member functions |
| Out-of-line pair ctor with `sizeof...` in dependent `typename` member init | `<tuple>`, `<functional>`, `<memory>` | `pair<_T1,_T2>::pair(...)` at `tuple:2877` uses `typename _Build_index_tuple<sizeof...(_Args1)>::__type()` in delegating ctor init |
| Ambiguous overloaded function in template body | `<vector>` | `_M_get_Tp_allocator()` call ambiguous between const and non-const overloads |
| Member variable template with concept constraint | `<variant>` | `__exactly_once` variable template using `__detail::__variant::__accepted_index` concept |
| IR conversion `assert` failure in arithmetic ops | `<span>` | Parsing succeeds but codegen crashes on non-integer/float arithmetic |
| `compare_exchange_weak` template | `<atomic>`, `<barrier>` | Template instantiation failure for member function with `__cmpexch_failure_order()` call |
| `<ratio>` heap corruption | `<ratio>` | Crash with malloc assertion failure during template instantiation |

### Recent Fixes (2026-02-10)

The following parser issues were fixed to unblock standard header compilation:

1. **Brace-init return for dependent types in template bodies**: When parsing `return { expr1, expr2 };` in a template body where the return type is a dependent `UserDefined` type (not yet resolved to a struct), the parser now treats it as a struct-like initializer instead of rejecting it as "too many initializers for scalar type". Unblocks `<map>`, `<set>` past `stl_tree.h:1534`.

2. **Dependent type alias base classes**: Type aliases used as base classes in template bodies (e.g., `_Tp_alloc_type`) are now allowed even when they can't be resolved to a struct type at parse time. The resolution is deferred to template instantiation. Unblocks `<vector>` past `stl_vector.h:133`.

3. **`if (Type var = expr)` declaration-as-condition**: The `if` statement parser now handles variable declarations as conditions (e.g., `if (size_type __n = this->finish - pos)`), where the declaration is implicitly converted to bool. Unblocks `<vector>` past `stl_vector.h:1945`.

4. **Error recovery in template struct bodies**: When parsing member declarations in template class bodies (both primary templates and partial specializations), parse errors in individual members now skip to the next semicolon/closing brace instead of aborting the entire struct. This allows parsing to continue past unsupported member patterns. Unblocks `<variant>` past `variant:694`.

5. **Pack expansion using-declarations (`using Base<Args>::member...;`)**: Using-declarations with C++17 pack expansion are now correctly parsed. The parser handles template arguments before `::` and the trailing `...` before `;` in both regular struct bodies and member struct template partial specialization bodies. Unblocks `<variant>` past `variant:815`.

**Headers with changed status:** `<vector>` progressed from base class error to ambiguous overload error. `<variant>` progressed from line 694 to line 831. `<map>` and `<set>` progressed past brace-init return error.

### Recent Fixes (2026-02-09, PR #3)

The following parser issues were fixed to unblock standard header compilation:

1. **C++20 constrained `auto` parameters**: `parse_type_and_name()` now detects when a UserDefined type is followed by `auto` keyword and converts the type to Auto, treating `ConceptName auto param` as an abbreviated function template. Unblocks `ranges_util.h:298` for `<tuple>`, `<functional>`, `<memory>`.

2. **Trailing `requires` clause on constructors**: Added `skip_trailing_requires_clause()` after `parse_constructor_exception_specifier()` in all 3 template constructor parsing paths (full specialization, partial specialization, member function template). The trailing requires clause like `requires Constraint` can appear between `noexcept(...)` and the member initializer list `:`. Unblocks `ranges_util.h:321`.

3. **Array reference parameters in deduction guides**: Deduction guide parameter parsing now handles the `Type(&)[Extent]` and `Type(&&)[Extent]` array reference pattern. This is used in `span(_Type(&)[_ArrayExtent]) -> span<_Type, _ArrayExtent>`. Unblocks `<span>` past deduction guide parsing.

4. **`::template rebind<T>::other` nested type access**: After parsing template member arguments in dependent qualified types (e.g., `__alloc_traits<_Alloc>::template rebind<_Tp>`), the parser now continues scanning for further `::member` access (e.g., `::other`). Unblocks `<vector>`, `<map>`, `<set>` past `stl_vector.h:87`.

5. **Template destructor calls `ptr->~Type<Args>()`**: Pseudo-destructor call parsing now skips template arguments between the type name and `()`, e.g., `__p->~_Rb_tree_node<_Val>()`. Unblocks `<map>`, `<set>` past `stl_tree.h:622`.

6. **C++ attributes after function name**: `parse_type_and_name()` now calls `skip_cpp_attributes()` after parsing the identifier, handling patterns like `as_writable_bytes [[nodiscard]] (span<_Type, _Extent> __sp)`. Unblocks `<span>` past `span:488`.

7. **Template brace-init type lookup with default args**: When looking up the instantiated type for `return Type<Args>{...}`, the code now falls back to the V2 instantiation cache to find the correct struct name when the type was registered with filled-in default template arguments. Unblocks `<string_view>`, `<string>`, `<iostream>`, `<ranges>` past `string_view:863`.

**Headers with changed status:** 5 headers (`<string_view>`, `<string>`, `<iostream>`, `<ranges>`, `<span>`) progressed from parse errors to codegen errors (parsing fully succeeds). 4 headers (`<tuple>`, `<functional>`, `<memory>`, `<vector>`) progressed to later parse errors. 2 headers (`<map>`, `<set>`) progressed to later parse errors.

### Recent Fixes (2026-02-09, PR #2)

The following parser issues were fixed to unblock standard header compilation:

1. **`bad_any_cast` crash in `allocator_traits` template instantiation**: Member function templates (e.g., `construct`, `destroy` in `allocator_traits<allocator<_Tp>>`) stored as `TemplateFunctionDeclarationNode` were not handled during class template instantiation. The code assumed all non-constructor/destructor members are `FunctionDeclarationNode`. Fixed in two locations in `try_instantiate_class_template()`. Unblocks 10+ headers past the `bad_any_cast` crash.

2. **`min()` static member function resolution**: When inside a class body, unqualified calls like `min()` found namespace-scope template functions (e.g., `std::min`) instead of the class's own static member function. Added class member priority check before template function instantiation, searching both `struct_parsing_context_stack_` and `member_function_context_stack_`. Unblocks `<string_view>`, `<span>`.

3. **`__cpp_exceptions` macro removed**: FlashCpp doesn't implement exception handling. Previously, defining `__cpp_exceptions` caused standard headers to include try/catch code paths that can't be parsed. Without it, headers use simpler non-exception fallback paths (e.g., `if(true)`/`if(false)` macros from `exception_defines.h`).

4. **SFINAE in requires expressions**: Template function instantiation failures inside requires expression bodies now create placeholder nodes instead of hard errors when `in_sfinae_context_` is true. This fixes `__is_derived_from_view_interface_fn` constraint evaluation in `ranges_base.h`.

5. **Compound requirement SFINAE handling**: In requires expression bodies, compound requirements (`{ expr } -> type;`) and simple requirements now gracefully handle expression parsing failures by skipping the requirement and adding a false literal (unsatisfied requirement), instead of propagating hard errors.

6. **User-defined literal operator parsing (`operator""sv`)**: Added parsing of user-defined literal operators in both operator parsing locations in `parse_type_and_name()`. Handles `operator""suffix` syntax for literals like `operator""sv`, `operator""_ms`. Progresses `<string_view>`.

**Headers with changed status:** All `bad_any_cast` crash headers (`<string>`, `<iostream>`, `<vector>`, `<map>`, `<set>`, `<tuple>`, `<functional>`, `<ranges>`, `<algorithm>`, `<memory>`) now progress past the crash to parse errors. `<string_view>` progresses to near end of file (line 862→863). `<span>` progresses to deduction guide parsing.

### Recent Fixes (2026-02-09)

The following parser issues were fixed to unblock standard header compilation:

1. **Conversion operator detection in partial template specialization bodies**: Added the same conversion operator detection logic (for `operator type()` patterns) to partial specialization body parsing that already existed for full specializations. This fixes `operator __pointer_type() const noexcept` in `template<typename _PTp> struct __atomic_base<_PTp*>`. Progresses `<atomic>` past the conversion operator error.

2. **Type alias resolution for out-of-line member definitions**: When resolving `ClassName::member` for out-of-line definitions, if the class name is a type alias (e.g., `using Alias = SomeStruct;`), the parser now follows `type_index_` to find the underlying struct type. Previously, type aliases returned no `StructTypeInfo`, causing "is not a struct/class type" errors. Progresses `<coroutine>`.

3. **Brace initialization for out-of-line static member definitions**: Added handling for `ClassName::member{}` syntax (brace-init) in out-of-line static member variable definitions, complementing the existing parenthesized initializer handling (`ClassName::member(value)`). This fixes patterns like `inline noop_coroutine_handle::_S_fr{};`. Progresses `<coroutine>`.

4. **`pending_explicit_template_args_` leak fix**: Cleared `pending_explicit_template_args_` at the start of each `parse_statement_or_declaration()` call. Template arguments parsed during one expression could leak into unrelated function calls in subsequent statements. This caused `__builtin_va_start(args, fmt)` to be incorrectly treated as a template function call after including `<bits/stl_iterator.h>`. Fixes `<ranges>` and `<iterator>` past the `__builtin_va_start` error.

5. **Double namespace prefix in template class registration**: When registering template classes in the template registry, the struct name from `StructDeclarationNode` could already be namespace-qualified (e.g., `std::numeric_limits`). Building a qualified name from this would produce `std::std::numeric_limits`, causing template lookup failures. Now extracts the unqualified name before building the qualified identifier. Fixes `std::numeric_limits` lookup for `<string_view>`, `<span>`.

**Headers with changed status:** `<ratio>` (memory corruption → parse error), `<ranges>` (parse error → bad_any_cast - progressed), `<atomic>` (conversion operator error → template instantiation error - progressed), `<coroutine>` (parse error → infinite loop - progressed), `<string_view>` (bad_any_cast → min() resolution error - progressed), `<span>` (bad_any_cast → min() resolution error - progressed).

### Recent Fixes (2026-02-08, PR #3)

The following parser issues were fixed to unblock standard header compilation:

1. **`explicit constexpr` constructor ordering in member function templates**: The constructor detection lookahead in `parse_member_function_template()` now calls `parse_declaration_specifiers()` after consuming the `explicit` keyword, allowing `explicit constexpr` (not just `constexpr explicit`). This matches the C++20 standard that permits these specifiers in any order. Unblocks `<optional>`.

2. **Deduction guide parameter pack expansion (`_Up...`)**: The deduction guide parameter parsing in `parse_template_declaration()` now handles `...` (pack expansion) after type specifiers. Previously, `array(_Tp, _Up...) -> array<_Tp, 1 + sizeof...(_Up)>;` would fail because `...` was not consumed. Unblocks `<array>`, `<span>`.

3. **Relative include resolution for quoted includes**: The preprocessor `#include "file.h"` directive now searches the directory of the including file first, per C++ standard [cpp.include]. Previously, `pstl/execution_defs.h` including `"algorithm_fwd.h"` (in the same `pstl/` directory) would fail because only the main file's directory was searched. Unblocks `<memory>`, `<algorithm>`.

4. **Ref-qualifier token type bug in `skip_function_trailing_specifiers()`**: Fixed `&` and `&&` ref-qualifier detection which was checking for `Token::Type::Punctuator` but these tokens are actually `Token::Type::Operator` (token kind `BitwiseAnd`/`LogicalAnd`). Now uses `peek() == "&"_tok` pattern for correct matching. Unblocks template member functions with `const&` and `&&` ref-qualifiers, critical for `<optional>`.

5. **`[[__deprecated__]]` C++ attributes on `using` type aliases**: Added `skip_cpp_attributes()` after the alias name in both member type alias and regular type alias parsing. This handles patterns like `using result_type [[__deprecated__]] = size_t;` in `<optional>` and `<functional>`.

**Headers newly compiling:** `<optional>` (was blocked by ref-qualifier, explicit constexpr, and attribute issues), `<any>` (was blocked by out-of-line template member issues), `<chrono>` (previously crashed with malloc assertion failure; fixes to ref-qualifier and attribute handling may have resolved the underlying memory corruption path).

### Recent Fixes (2026-02-05)

The following parser issues were fixed to unblock standard header compilation:

1. **`__typeof(function_name)`**: `get_expression_type()` now handles `FunctionDeclarationNode` identifiers, returning the function's return type. This unblocks `c++locale.h` which uses `extern "C" __typeof(uselocale) __uselocale;`.

2. **`__builtin_va_list` / `__gnuc_va_list`**: Registered as built-in types in `initialize_native_types()` and handled in `parse_type_specifier()`. This unblocks `c++locale.h` and any code using variadic argument types directly.

3. **Unnamed bitfields (`int :32;`)**: `parse_type_and_name()` now recognizes `:` and `;` as valid terminators for unnamed declarations, and struct member parsing consumes the bitfield width expression. This unblocks `bits/timex.h` (included via `<time.h>` → `<pthread.h>`).

4. **Function pointer params with pointer return types**: Added a second function pointer detection check in `parse_type_and_name()` after pointer levels are consumed. The pattern `void *(*callback)(void *)` was previously not detected because the `(` check happened before `*` was consumed. This unblocks `<pthread.h>` function declarations.

5. **C-style `(void)` parameter lists**: `parse_parameter_list()` now treats `(void)` as equivalent to `()` (no parameters). This unblocks many C library function declarations like `pthread_self(void)`.

**Remaining blocker for `<iostream>`, `<atomic>`, `<barrier>`**: The `pthread_self()` function call in `gthr-default.h:700` fails with "No matching function" — the function is declared and found in the symbol table, but call resolution doesn't match the `(void)` signature. This is now fixed by the `(void)` parameter list change above.

### C Library Wrappers (Also Working)

| Header | Test File | Notes |
|--------|-----------|-------|
| `<cstddef>` | `test_cstddef.cpp` | `size_t`, `ptrdiff_t`, `nullptr_t` (~0.13s) |
| `<cstdlib>` | `test_cstdlib.cpp` | `malloc`, `free`, etc. (~0.05s) |
| `<cstdio>` | `test_cstdio_puts.cpp` | `printf`, `puts`, etc. (~0.12s) |
| `<cstdint>` | N/A | `int32_t`, `uint64_t`, etc. (~0.04s) |
| `<cstring>` | N/A | `memcpy`, `strlen`, etc. (~0.12s) |
| `<ctime>` | N/A | `time_t`, `clock`, etc. (~0.08s) |
| `<climits>` | N/A | `INT_MAX`, `LONG_MAX`, etc. (~0.03s) |
| `<cfloat>` | N/A | `FLT_MAX`, `DBL_MIN`, etc. (~0.04s) |
| `<cassert>` | N/A | `assert` macro (~0.04s) |
| `<cerrno>` | N/A | `errno` (~0.03s) |
| `<clocale>` | N/A | `setlocale`, `localeconv` (~0.04s) |
| `<cstdarg>` | N/A | `va_list`, `va_start`, etc. (~0.03s) |
| `<cfenv>` | N/A | `fenv_t`, `fegetenv`, etc. (~0.03s) |
| `<cinttypes>` | N/A | `imaxabs`, `imaxdiv`, etc. (~0.04s) |
| `<cctype>` | N/A | `isalpha`, `isdigit`, etc. (~0.05s) |
| `<cuchar>` | N/A | `char16_t`, `char32_t` conversions (~0.13s) |
| `<cwchar>` | N/A | `wchar_t` functions (~0.56s) |
| `<cwctype>` | N/A | `iswupper`, `iswlower`, etc. (~0.78s) (2026-01-16: Fixed parenthesized identifier followed by `<`) |
| `<cstdbool>` | N/A | C99 `bool` compatibility (~0.13s) |
| `<cstdalign>` | N/A | C11 alignment specifiers (~0.13s) |
| `<ciso646>` | N/A | Alternative operator spellings (~0.03s) |

## Running the Tests

```bash
cd tests/std
./test_std_headers_comprehensive.sh
```

## Viewing Parser Progress

To see progress logging during compilation, build with Info level logging enabled:

```bash
# Build release with progress logging
clang++ -std=c++20 -DFLASHCPP_LOG_LEVEL=2 -O3 -I src -o x64/InfoRelease/FlashCpp \
    src/AstNodeTypes.cpp src/ChunkedAnyVector.cpp src/Parser.cpp \
    src/CodeViewDebug.cpp src/ExpressionSubstitutor.cpp src/main.cpp

# Run with progress output
./x64/InfoRelease/FlashCpp tests/std/test_std_type_traits.cpp
# Output: [Progress] 100 template instantiations in 1 ms (cache hit rate: 44.4%)
#         [Progress] 200 template instantiations in 4 ms (cache hit rate: 55.3%)
#         [Progress] Parsing complete: 7 top-level nodes, 58 AST nodes in 9 ms
```

### Template Profiling Statistics

For detailed template instantiation statistics, use the `--perf-stats` flag:

```bash
./x64/Release/FlashCpp test.cpp --perf-stats
# Shows:
#   - Template instantiation counts and timing
#   - Cache hit/miss rates
#   - Top 10 most instantiated templates
#   - Top 10 slowest templates
```

## Disabling Logging

Logging can be controlled at runtime and compile-time.

### Runtime Log Level Control

```bash
# Set global log level
./x64/Debug/FlashCpp file.cpp --log-level=warning

# Set log level for specific category
./x64/Debug/FlashCpp file.cpp --log-level=Parser:debug

# Available levels: error (0), warning (1), info (2), debug (3), trace (4)
# Available categories: General, Parser, Lexer, Templates, Symbols, Types, Codegen, Scope, Mangling, All
```

### Compile-time Log Level Control

```bash
# Build with specific log level (0=error, 1=warning, 2=info, 3=debug, 4=trace)
# Note: Default release build uses level 1 (warning only)
# Use level 2 to include Info messages (including progress logging)
clang++ -DFLASHCPP_LOG_LEVEL=2 -O3 ...
```

## Adding New Standard Header Tests

1. Create `test_std_<header>.cpp`:
   ```cpp
   #include <header>
   int main() { return 0; }
   ```

2. Verify valid C++20: `clang++ -std=c++20 -c test_std_<header>.cpp`

3. Test: `./test_std_headers_comprehensive.sh`

4. If it fails, add to `EXPECTED_FAIL` in `../run_all_tests.sh`

## See Also

- [`STANDARD_HEADERS_MISSING_FEATURES.md`](./STANDARD_HEADERS_MISSING_FEATURES.md) - Detailed analysis of missing features and implementation history

