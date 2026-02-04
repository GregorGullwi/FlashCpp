# Standard Header Tests

This directory contains test files for C++ standard library headers to assess FlashCpp's compatibility with the C++ standard library.

## Current Status

| Header | Test File | Status | Notes |
|--------|-----------|--------|-------|
| `<limits>` | `test_std_limits.cpp` | ✅ Compiled | ~29ms |
| `<type_traits>` | `test_std_type_traits.cpp` | ❌ Parse Error | Parses 400 templates (~100ms), static_assert constexpr evaluation issue |
| `<compare>` | N/A | ✅ Compiled | ~258ms (2026-01-24: Fixed with operator[], brace-init, and throw expression fixes) |
| `<version>` | N/A | ✅ Compiled | ~17ms |
| `<source_location>` | N/A | ✅ Compiled | ~17ms |
| `<numbers>` | N/A | ✅ Compiled | ~33ms |
| `<initializer_list>` | N/A | ✅ Compiled | ~16ms |
| `<ratio>` | `test_std_ratio.cpp` | ❌ Parse Error | static_assert constexpr evaluation (~155ms) |
| `<vector>` | `test_std_vector.cpp` | ⏱️ Timeout | Template complexity causes timeout |
| `<tuple>` | `test_std_tuple.cpp` | ⏱️ Timeout | Template complexity causes timeout |
| `<optional>` | `test_std_optional.cpp` | ❌ Parse Error | Parses 700+ templates (~280ms), parse error at optional:564 (in_place_t parameter) |
| `<variant>` | `test_std_variant.cpp` | ❌ Parse Error | Parses 550+ templates (~170ms), static_assert constexpr at parse_numbers.h:198 |
| `<any>` | `test_std_any.cpp` | ❌ Parse Error | Parses 500+ templates (~153ms), nested out-of-line template member function at any:583 |
| `<concepts>` | `test_std_concepts.cpp` | ✅ Compiled | ~100ms |
| `<utility>` | `test_std_utility.cpp` | ✅ Compiled | ~311ms (2026-01-30: Fixed with dependent template instantiation fix) |
| `<bit>` | N/A | ❌ Parse Error | Progresses past char_traits.h (2026-02-03), likely blocked at similar point as string |
| `<string_view>` | `test_std_string_view.cpp` | ❌ Parse Error | Parses 650+ templates (~263ms), progresses past char_traits.h:534 (2026-02-03) |
| `<string>` | `test_std_string.cpp` | ❌ Parse Error | Parses 650+ templates (~262ms), progresses to new_allocator.h:131 (2026-02-03) |
| `<array>` | `test_std_array.cpp` | ⏱️ Timeout | Template complexity causes timeout |
| `<memory>` | `test_std_memory.cpp` | ❌ Include Error | Test file missing |
| `<functional>` | `test_std_functional.cpp` | ⏱️ Timeout | Template complexity causes timeout |
| `<algorithm>` | `test_std_algorithm.cpp` | ❌ Include Error | Test file missing |
| `<map>` | `test_std_map.cpp` | ⏱️ Timeout | Template complexity causes timeout |
| `<set>` | `test_std_set.cpp` | ⏱️ Timeout | Template complexity causes timeout |
| `<span>` | `test_std_span.cpp` | ⏱️ Timeout | Template complexity causes timeout |
| `<ranges>` | `test_std_ranges.cpp` | ⏱️ Timeout | Template complexity causes timeout |
| `<iostream>` | `test_std_iostream.cpp` | ❌ Parse Error | Progresses to c++locale.h:52 (2026-02-03) - __typeof keyword issue |
| `<chrono>` | `test_std_chrono.cpp` | ❌ Include Error | Test file missing |
| `<atomic>` | N/A | ❌ Parse Error | Missing `pthread_t` identifier (pthreads types) |
| `<new>` | N/A | ✅ Compiled | ~18ms |
| `<exception>` | N/A | ✅ Compiled | ~43ms |
| `<typeinfo>` | N/A | ✅ Compiled | ~18ms |
| `<typeindex>` | N/A | ❌ Parse Error | Out-of-line template member functions |
| `<csetjmp>` | N/A | ✅ Compiled | ~16ms |
| `<csignal>` | N/A | ✅ Compiled | ~22ms |
| `<stdfloat>` | N/A | ✅ Compiled | ~14ms (C++23) |
| `<spanstream>` | N/A | ✅ Compiled | ~17ms (C++23) |
| `<print>` | N/A | ✅ Compiled | ~17ms (C++23) |
| `<expected>` | N/A | ✅ Compiled | ~18ms (C++23) |
| `<text_encoding>` | N/A | ✅ Compiled | ~17ms (C++26) |
| `<barrier>` | N/A | ❌ Parse Error | Missing `pthread_t` identifier (pthreads types) |
| `<stacktrace>` | N/A | ✅ Compiled | ~17ms (C++23) |
| `<coroutine>` | N/A | ❌ Parse Error | Out-of-line template member functions |

**Legend:** ✅ Compiled | ❌ Failed/Parse/Include Error | ⏱️ Timeout (60s) | 💥 Crash


**Note (2026-02-03 Latest Update - This PR):** Fixed two critical blockers affecting string-related headers:
1. **Wide character functions (wmemcmp, wmemchr, wmemcpy, wmemmove, wcslen)** - Added registration for all 5 wide character functions used by char_traits.h. Extended the builtin function special case in overload resolution to:
   - Accept any number of overloads (not just 1) since standard library can have forward declarations
   - Accept UserDefined types (template parameters) as compatible with primitive types for flexible matching
   - Apply to functions starting with `wmem` or `wcs` in addition to `__builtin_`
2. **Empty statements (null statements)** - Added support for standalone semicolons in statement context. This commonly occurs when macros expand to nothing but leave a semicolon (e.g., `__glibcxx_assert(x);` expands to `;`).
- **Impact:**
  - `<string>` now progresses from char_traits.h:512 (wmemcmp) to new_allocator.h:131 (static_assert for incomplete types)
  - `<string_view>` now progresses past char_traits.h:534 (wmemchr) - similar progress to string
  - `<iostream>` now progresses from char_traits.h:512 to c++locale.h:52 (__typeof keyword issue)
  - `<optional>` now progresses from optional:475 (empty statement) to optional:564 (~700 templates parsed)
  - `<bit>` also passes the wmemcmp/wmemchr blockers
- **Remaining issues:**
  - __typeof keyword parsing needed for iostream
  - static_assert with incomplete types in allocator headers
  - Nested out-of-line template members for <any>
  - Static assert constexpr evaluation for <type_traits>, <ratio>, <variant>

**Note (2026-02-03 Previous Update):** Fixed three critical blockers affecting most standard library headers:
1. **Type alias resolution in base classes** - When inheriting from `Template<T>::type` patterns, the parser now properly resolves the type alias to its underlying type instead of treating the qualified name as the base. Also added support for dependent template placeholders (e.g., `integral_constant$hash`) as base classes.
2. **Nested type lookup for dependent placeholders** - When accessing `::type` on dependent template placeholders (e.g., `remove_cv$hash::type`), the parser now creates nested placeholders instead of failing with "Unknown nested type". This enables proper parsing of complex type trait expressions.
3. **Builtin function overload resolution** - Added special case for `__builtin_*` functions to accept pointer arguments even when the exact pointer type doesn't match. This fixes `__builtin_strlen` and similar builtins where type aliases prevent exact type matching.
- **Impact:**
  - `<string>`, `<string_view>`, `<bit>`, `<iostream>` now parse 650+ templates and progress from type_traits:462 to char_traits.h:512 (wmemcmp)
  - `<optional>` now parses 700+ templates and progresses to optional:475 (unexpected semicolon)
  - `<any>` now parses 500+ templates and progresses to any:583 (nested out-of-line template member)
  - `<variant>` now parses 550+ templates and progresses to parse_numbers.h:198 (static_assert constexpr)
  - All headers that were blocked by `integral_constant::type` base class issues now progress significantly further
- **Remaining issues:**
  - Missing standard C library functions (wmemcmp, etc.) for wide character support
  - Nested out-of-line template members (`Outer::Inner<T>::method`) not yet supported
  - Static assert constexpr evaluation for type trait values
  - Template complexity/performance for container headers

**Note (2026-01-31 Latest Update):** Fixed three major blockers for standard library headers:
1. **For loop initialization with type aliases** - Parser now recognizes type aliases (like `size_t`) in for loop initialization by checking `gTypesByName`. Previously only recognized language keywords as type names.
2. **Inheriting constructors** - Parser now recognizes and handles `using BaseClass<T>::BaseClass;` syntax. Added support in `parse_member_type_alias` with lookahead to detect template arguments before `::`.
3. **Out-of-line template member functions with pointer/reference return types** - Fixed `try_parse_out_of_line_template_member` to skip pointer and reference modifiers after the return type. This handles multi-line declarations like:
   ```cpp
   template<typename T>
   const typename Class<T>::nested_type*
   Class<T>::method(...) { ... }
   ```
- **Impact:** 
  - **NEW:** `<string>`, `<string_view>`, `<bit>`, `<iostream>` now progress past char_traits.h:373 for loop error to line 391 (__builtin_strlen lookup issue)
  - `<optional>` progresses past line 337 (inheriting constructors) to line 475 (unrelated parsing issue)
  - `<span>`, `<ranges>`, `<map>`, `<set>` now parse significantly more before hitting timeout (likely due to reduced backtracking)
  - `<any>` progresses to line 583 (nested out-of-line: `Outer::Inner<T>::method`)
- **Remaining issues:**
  - Nested out-of-line template members (`Outer::Inner<T>::method`) not yet supported
  - Builtin function lookup (`__builtin_strlen`) needs fixing for string headers
  - Template instantiation in constexpr context (e.g., `is_integral<int>::value`) needs architecture changes

**Note (2026-01-30 Latest Update):** Fixed dependent template instantiation to preserve template argument names in mangled type names. When a template like `is_function<_Tp>` is parsed inside a template body, it's now registered as `is_function__Tp` (a placeholder preserving the dependent type info) instead of falling back to `is_function` (the primary template). This fixes the issue where nested template instantiations like `__not_<__or_<is_function<_Tp>, ...>>` would lose their dependent type information. Also improved the `contains_template_param` check to recognize underscore-prefixed parameters (like `_Tp`) in mangled names.
- **Impact:** `<utility>` now compiles successfully! Many other headers now parse significantly more templates before hitting their respective blockers.

**Note (2026-01-23 Latest Update):** Fixed multiple `<compare>` header blockers including variable template lookup with dependent args, constructor parsing in member struct templates, and inline constexpr struct with trailing variable initializers. The header now progresses from line 763 to line 1210, failing on `requires requires` clause in inline constexpr struct member function (`_Synth3way::operator()`).

**Note (2026-01-23 Standard Headers Progress):** Fixed multiple blockers in `iterator_concepts.h` and related headers. The `<vector>` header now successfully parses past `iterator_concepts.h` (was failing at line 423, 789, 958, 974) and `stl_iterator_base_types.h` (was failing at line 126). Currently blocked at `stl_construct.h:96` due to placement new in decltype expression (`decltype(::new((void*)0) _Tp(...))`).

**Note (2026-01-23 Previous Update):** Fixed block scope handling in if/else statements (fixes `<any>` variable redeclaration error). Fixed SFINAE context for requires expression bodies so function lookup failures no longer produce errors. Disabled `__cpp_using_enum` macro since parser doesn't support "using enum" yet. The `<compare>` header now progresses past line 621 but fails at line 763 due to template constructor parsing issues.

**Note (2026-01-22 Evening Update):** All timeout issues have been resolved! The infinite loop bug in the parser has been fixed. Headers that were timing out now complete in 100-200ms. The remaining blockers are actual parsing/semantic issues.

**Note (2026-01-22 Later Update):** Major `<compare>` header parsing fixes applied! The `<compare>` header now parses successfully. Many headers that were blocked by `<compare>` parse errors now progress to semantic errors (function lookup failures).

**Note (2026-01-23 Update):** Implemented deferred body parsing for member template constructors. This allows template constructors to access member variables that are declared later in the class definition (complete-class context). However, non-template inline member functions inside class bodies still have issues accessing forward-declared members.

**Note (2026-01-23 Later Update):** Fixed partial specialization forward declarations (`template<typename T> struct X<T*>;`) which were causing the parser to incorrectly enter struct body mode. Also fixed qualified concept lookup so namespaced concepts like `std::same_as<T>` work correctly. Fixed parenthesized concept expressions in constraints (e.g., `(concept<T>) && ...`). The `<optional>` header now progresses to semantic errors (function lookup).

**Note (2026-01-24 Latest Update):** Fixed `operator[]` parsing in template class bodies, brace initialization of structs with constructors but no data members, and throw expressions as unary operators. The `<compare>` header now fully compiles. Fixed union template parsing - union keyword now recognized in all template declaration paths. The `<optional>` header now progresses past line 204 and fails at line 141 with a different constexpr evaluation error.

**Primary Remaining Blockers:**
1. **Missing standard C library functions** - Functions like `wmemcmp`, `wmemchr`, `wmemcpy` are not registered. This blocks `<string>`, `<string_view>`, `<iostream>`, `<bit>` at char_traits.h:512. Easy fix: register these functions similar to `__builtin_strlen`.
2. **Nested out-of-line template member functions** - Patterns like `template<typename T> void Outer::Inner<T>::method()` are not supported. Single-level out-of-line members (`Class<T>::method`) now work, but nested versions need additional parsing support. This affects `<any>` (line 583: `any::_Manager_internal<_Tp>::_S_manage`).
3. **Static assert constexpr evaluation** - Templates like `is_integral<int>` inherit from `integral_constant<bool, true>::type`. While parsing now works, constexpr evaluation of `::value` through the inheritance chain fails. This affects `static_assert` statements in `<type_traits>`, `<ratio>`, `<variant>`.
4. **Template complexity/performance** - Headers like `<vector>`, `<tuple>`, `<array>`, `<functional>`, `<map>`, `<set>`, `<span>`, `<ranges>` time out due to template instantiation complexity.
5. **Missing pthread types** - `<atomic>` and `<barrier>` need pthread support

**Fixes Applied (2026-01-31 This PR - For Loop Initialization with Type Aliases):**
- **Fixed** For loop initialization with type aliases (e.g., `size_t`, custom types)
  - Modified `parse_for_loop` to check `gTypesByName` for type aliases when parsing initialization
  - Previously only recognized language keywords (int, char, etc.) as valid type names in for loop init
  - Now handles patterns like `for (size_t __i = 0; __i < __n; ++__i)` correctly
  - Solution: Added identifier check that looks up the name in `gTypesByName` before deciding to parse as declaration vs expression
- **Impact:**
  - `<string>`, `<string_view>`, `<iostream>`, `<bit>` progress from char_traits.h:373 (for loop parse error) to line 391 (__builtin_strlen lookup)
  - Unblocks all headers that use standard type aliases in for loop initialization
  - Fixes a fundamental limitation where only built-in type keywords were recognized in certain contexts

**Fixes Applied (2026-01-31 This PR - Inheriting Constructors and Out-of-line Template Members):**
- **Fixed** Inheriting constructors syntax: `using BaseClass<T>::BaseClass;`
  - Modified `parse_member_type_alias` to detect when the imported member name matches the base class name
  - Added template argument skipping in lookahead to handle `using Base<T>::Base;` pattern
  - Updated `parse_member_struct_template` to handle inheriting constructors in member struct templates
  - Added `has_inherited_constructors` flag to `StructParsingContext`
- **Fixed** Out-of-line template member functions with pointer/reference return types
  - Modified `try_parse_out_of_line_template_member` to skip `*`, `&`, `&&` modifiers and cv-qualifiers after return type
  - Handles multi-line declarations where return type is on a separate line from class qualifier
  - Example patterns now supported:
    ```cpp
    template<typename T>
    const typename Class<T>::nested_type*
    Class<T>::method(...) { ... }
    ```
- **Impact:**
  - `<optional>` progresses from line 337 (inheriting constructors) to line 475 (~650 templates parsed)
  - `<string>`, `<string_view>`, `<bit>`, `<iostream>` progress past char_traits.h:210 (out-of-line member) to line 373
  - `<any>` progresses from line 574 to line 583 (nested out-of-line member)
  - `<map>`, `<set>`, `<span>`, `<ranges>` now parse more before timeout (reduced backtracking from failed out-of-line parses)

**Fixes Applied (2026-01-25 This PR - `this` keyword support):**
- **Fixed** `this` keyword in statement context - Added `{"this", &Parser::parse_expression_statement}` to keyword_parsing_functions map (Parser.cpp:~14078)
  - The `this` keyword was already supported in expression context but not recognized when starting a statement
  - Patterns like `this->member = value;` and `return this->member;` now work correctly
  - Test case: `test_this_keyword.cpp` compiles and returns 42
- **Impact:** `<optional>` header progresses past line 141 (unknown keyword `this`) to line 337 (inheriting constructors). Many other headers that use `this->` syntax will also benefit.

**Investigation: Type Alias Resolution Issue (2026-01-25):**
- **Problem**: Templates that inherit from a base class via type alias (e.g., `struct Derived : Base<T>::type {}`) fail to find static members through inheritance
- **Root Cause**: When `lookup_inherited_type_alias` returns a TypeInfo* for the alias, the `type_index_` field is invalid (out of bounds). This appears to be a stale pointer from before `gTypeInfo` vector was resized during template instantiation.
- **Test Case**: `is_integral<int>` → `__is_integral_helper<int>::type` → `integral_constant<bool, true>` 
  - The `::type` alias is found successfully
  - But the alias TypeInfo has `type_index_=93` when `gTypeInfo.size()=33`
  - This prevents the base class from being added with the correct underlying type name
  - As a result, `is_integral<int>::value` cannot be found through the inheritance chain
- **Impact**: Blocks `<type_traits>`, `<ratio>`, and transitively most of the standard library
- **Next Steps**: Need to either (1) fix type alias `type_index_` updates when `gTypeInfo` is resized, (2) store type aliases separately from `gTypeInfo`, or (3) look up underlying types by name (slower but more reliable)

**Fixes Applied (2026-01-25 Previous - Template Parameter Substitution in Static Members):**
- **Added** ExpressionSubstitutor support for static member initializers during template instantiation
  - Modified `try_instantiate_class_template` to use ExpressionSubstitutor for pattern AST static members (Parser.cpp:~37780)
  - Modified `instantiateLazyStaticMember` to use ExpressionSubstitutor as fallback for template-dependent expressions (Parser.cpp:~42465)
  - Added variable template instantiation support to ExpressionSubstitutor (ExpressionSubstitutor.cpp:~291)
- **Fixed** Non-type template parameter substitution in `substituteIdentifier`:
  - BoolLiteralNode created for bool non-type parameters (e.g., `integral_constant<bool, true>`)
  - NumericLiteralNode created for numeric non-type parameters (e.g., `integral_constant<int, 42>`)
  - This fixes bad_any_cast errors that were breaking `test_integral_constant_pattern_ret42.cpp`
- **Added** Base class static member support in constexpr evaluation:
  - Modified `try_evaluate_constant_expression` to use `findStaticMemberRecursive` instead of `findStaticMember` (Parser.cpp:~32732)
  - Added lazy instantiation trigger for base class static members when accessed through derived classes
- **Added** Lazy static member instantiation trigger during qualified identifier parsing (Parser.cpp:~18493):
  - Triggers instantiation when parsing `Type::member` patterns
  - Handles type alias resolution to find actual instantiated template types
  - Resolves aliases like `true_type` to `integral_constant<bool, true>` before triggering instantiation
- **Impact:** 
  - Successfully handles simple template parameter substitutions like `sizeof(T)` and direct template parameter references
  - Non-type template parameters now correctly substituted with literal values
  - Test `test_integral_constant_pattern_ret42.cpp` now compiles and returns 42 correctly (was crashing with bad_any_cast)
  - Test `test_integral_constant_comprehensive_ret100.cpp` returns 100 correctly
- **Remaining Limitation:** Constexpr variable initialization (e.g., `constexpr bool t = Type::value;`) uses a code path that doesn't trigger lazy instantiation. This affects `test_integral_constant_simple_ret30.cpp` which returns 20 instead of 30. Full solution requires identifying all code paths where constexpr variables are initialized and ensuring lazy instantiation is triggered.
- **Verified:** Placement new in decltype (`decltype(::new((void*)0) T())`) compiles successfully - not a blocker for `<vector>`

**Fixes Applied (2026-01-24 This PR - Union Templates):**
- **Fixed** Union template parsing - Added union keyword support to template declaration recognition paths:
  - `parse_template_declaration()` now recognizes `template<...> union Name` patterns
  - Member union templates (`template<...> union` inside struct/class bodies) now parse correctly
  - Partial specialization and full specialization paths support union templates
  - Both top-level and nested union templates work
- **Impact:** `<optional>` header progresses from line 204 (union template error) to line 141 (constexpr evaluation error). Union templates with default template arguments and member templates now compile successfully.

**Fixes Applied (2026-01-24 Previous Updates):**
- **Fixed** `operator[]` parsing in template class bodies - The `[` token is a Punctuator not an Operator, so it was falling through to the conversion operator fallback path. Patterns like `reference operator[](difference_type n) const` now parse correctly.
- **Fixed** Brace initialization for structs with constructors but no data members - Patterns like `inline constexpr nullopt_t nullopt { nullopt_t::_Construct::_Token };` now use constructor initialization instead of aggregate initialization.
- **Fixed** Throw expressions as unary operators - Added `ThrowExpressionNode` to handle `throw` in expression context. Patterns like `(throw bad_optional_access())` now parse correctly.

**Fixes Applied (2026-01-23 Standard Headers PR):**
- **Fixed** Constrained partial specializations with requires clauses - Qualified names in requires clauses (e.g., `__detail::A<_Iter>`) now handled correctly
- **Fixed** Multiple constrained partial specializations with same pattern - Added unique counter for constrained partial specializations
- **Fixed** Constrained template parameters (concept directly on template param) - e.g., `template<weakly_incrementable _Iter, typename _Proj>`
- **Fixed** Nested struct/class declarations in partial specialization body parsing
- **Fixed** Friend function templates with constrained parameters and inline definitions - e.g., `template<Concept _It> friend constexpr bool operator==(...) { return false; }`
- **Fixed** Struct definition with immediate inline constexpr variable declaration - e.g., `struct _Decay_copy final { ... } inline constexpr __decay_copy{};`
- **Fixed** Storage class specifiers and brace initialization after struct body
- **Fixed** Pointer types as template parameter default values - e.g., `typename _Pointer = _Tp*`
- **Fixed** Pointer/reference types in trailing return types - e.g., `auto test() -> T*`

**Fixes Applied (2026-01-23 This PR):**
- **Fixed** Variable template lookup with dependent arguments - When a variable template is found but can't be instantiated due to dependent args, create placeholder node instead of error
- **Fixed** Constructor parsing in member struct templates - Properly skip constructors when parsing member struct template bodies
- **Fixed** Member function body parsing in member struct templates - Skip function bodies to defer until template instantiation
- **Fixed** Inline constexpr struct detection - `inline constexpr struct Name { ... } var = {};` pattern now delegates to struct parsing
- **Fixed** Trailing variable initializers after struct definition - `struct S {} s = {};` initializer syntax now supported
- **Refactored** Specifier parsing to use `parse_declaration_specifiers()` for common keywords
- **Fixed** Block scope handling in if/else statements - Variables declared in different branches of if/else are now correctly scoped
- **Fixed** SFINAE context in requires expression bodies - Function lookup failures inside requires expressions no longer produce errors
- **Fixed** `__cpp_using_enum` macro disabled - Parser doesn't support "using enum" statement yet, so fallback code paths are now used
- **Fixed** Member template constructor deferred body parsing - Template constructor bodies are now deferred until after the full class is parsed, enabling access to forward-declared member variables (complete-class context)
- **Fixed** Trailing `requires` clause support for static member functions - patterns like `static int f(int& r) requires requires { r; } { ... }` now parse correctly
- **Fixed** Function parameter scope in trailing requires clauses - parameters are now visible inside the requires expression
- **Fixed** Dependent member template access syntax `::template` - patterns like `typename _Tp::template rebind<_Up>` now parse correctly
- **Fixed** Partial specialization forward declarations - `template<typename T> struct X<T*>;` forward declarations now parse correctly
- **Fixed** Qualified concept lookup - Concepts in namespaces (like `std::same_as<T>`) are now looked up correctly in expressions
- **Fixed** Parenthesized concept expressions in constraints - `(concept<T>) && ...` patterns no longer parsed as C-style casts
- **Fixed** Empty template argument lists with `>>` token splitting - `__void_t<>>` patterns now parse correctly

**Fixes Applied (2026-01-22 This PR):**
- **Fixed** Out-of-line static constexpr member variable definition with parenthesized initializer (`partial_ordering::less(__cmp_cat::_Ord::less)`)
- **Fixed** `[[nodiscard]]` and other attributes before conversion operators in struct bodies
- **Fixed** Free-standing operator call syntax `operator<=>(args)` in requires expressions
- **Fixed** Member operator call syntax `obj.operator<=>(args)` 
- **Fixed** Postfix operators after cast expressions (`static_cast<T>(x).operator<=>()`)
- **Fixed** `operator() [[nodiscard]] (...)` attribute placement between operator name and parameter list
- **Fixed** `>>` token splitting in template parameter lookahead for member template aliases
- **Fixed** Pointer-to-member-function parameter syntax: `Ret (T::*identifier)(params)` and `Ret (T::*identifier)(params) const`

**Fixes Applied (2026-01-22 Earlier):**
- **Fixed** `std::bad_any_cast` crash in `<functional>` - member template functions were incorrectly cast
- **Fixed** `decltype(auto)` return type specifier (C++14 feature)
- **Fixed** Nested struct/template with base classes in template class bodies
- **Fixed** Infinite loop when parser reaches EOF - `peek_token()` now returns `std::nullopt` for EndOfFile tokens, causing all `while (peek_token().has_value())` loops to properly terminate

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

## Current Blockers

### 0. Infinite Loop at EOF (**FIXED** - 2026-01-22)

**Status:** **FIXED** in this PR

When parsing source files, the parser would spin forever after reaching the end of file. Multiple loops in the parser used `while (peek_token().has_value())` to iterate, but `peek_token()` always returned a valid optional - even for EOF tokens (with type `EndOfFile` and empty value).

**Root Cause:** The lexer returns an `EndOfFile` token at the end, which still has a value (empty string). All loops checking `peek_token().has_value()` would continue indefinitely because they expected `has_value()` to return `false` at EOF.

**Symptoms:**
- Standard header tests appearing to "timeout" after 400-500 templates
- Process spinning at 100% CPU with no output
- Memory not growing (not actually doing work, just spinning)

**Fix:** Modified `peek_token()` to return `std::nullopt` when the current token is `EndOfFile`:
```cpp
std::optional<Token> Parser::peek_token() {
    if (!current_token_.has_value()) {
        current_token_.emplace(lexer_.next_token());
    }
    // Return nullopt for EndOfFile to make loops terminate at EOF
    if (current_token_->type() == Token::Type::EndOfFile) {
        return std::nullopt;
    }
    return current_token_;
}
```

**Impact:** All standard header tests that were timing out now complete in 100-200ms. The issues revealed are now actual parsing/semantic errors, not infinite loops.

---

### 1. Pending Template Arguments Leak (**FIXED** - 2026-01-22)

**Status:** **FIXED** in commit 26a6675

When parsing `Template<T>::template member` syntax, the `pending_explicit_template_args_` was not cleared when the `::template` keyword was encountered or on error paths. This caused the template arguments to "leak" to unrelated function calls later in the same scope.

**Root Cause:** Parsing expressions like `__xref<_Tp2>::template __type` would:
1. Set `pending_explicit_template_args_` for `__xref` 
2. Encounter `::template` which wasn't handled
3. Return an error without clearing pending args
4. Leaked args would be applied to subsequent function calls like `name()`

**Fix:**
1. Handle `::template` syntax for dependent names
2. Clear `pending_explicit_template_args_` before returning errors

**Impact:** Fixed `<exception>` + `<type_traits>` combination, `<optional>` now progresses further.

---

### 2. Context-Dependent Parse Error in `bits/utility.h` (**FIXED** - 2026-01-22)

**Status:** **FIXED** - This was fixed by the pending template arguments leak fix.

**Test Result:**
```bash
$ FlashCpp -c test_utility.cpp   # includes <type_traits> + <bits/utility.h>
[Progress] Preprocessing complete: 7074 lines
# SUCCESS - compiles without errors
```

---

### 3. Remaining Blockers

#### 3.1 Constructor and Member Functions with `noexcept = delete` in Partial Specializations (**FIXED** - 2026-01-21)

**Issue:** ~~The `<variant>` header fails with a parse error for constructors and member functions marked `noexcept = delete` in template partial specializations.~~ **RESOLVED**

**Previous Error Messages:**
```
/usr/include/c++/14/bits/enable_special_members.h:119:61: error: Expected type specifier
      operator=(_Enable_default_constructor const&) noexcept = default;
                                                              ^

/usr/include/c++/14/bits/enable_special_members.h:130:6: error: Expected type specifier
    { ~_Enable_destructor() noexcept = delete; };
       ^
```

**Problematic Code Patterns:**
```cpp
// In template partial specialization:
Type& operator=(const Type&) noexcept = default;
~Destructor() noexcept = delete;
```

**Root Cause:** Partial specialization body parsing didn't call `parse_function_trailing_specifiers()` to handle trailing specifiers (noexcept, override, final, = default, = delete) on member functions and destructors.

**Fix Applied:** 
1. Added `parse_function_trailing_specifiers()` call after parsing member function parameters in partial specializations (Parser.cpp ~line 27050)
2. Added destructor parsing support in partial specialization bodies with full trailing specifiers support (Parser.cpp ~line 27010)
3. Both defaulted and deleted functions/destructors are now properly handled

**Impact:** The `<variant>` header now progresses from line 119 (operator=) to line 72 (complex decltype in partial specialization pattern). This fix unblocks many headers that use `bits/enable_special_members.h`.

**Test Case:** Created `/tmp/test_operator_eq_template.cpp` which now compiles successfully

#### 2.3 Complex decltype in Partial Specialization Template Arguments (**FIXED** - 2026-01-21)

**Issue:** Partial specializations with complex decltype expressions containing nested template instantiations and function calls fail to parse.

**Status:** **FIXED** - Added handling in `parse_primary_expression` to recognize class templates followed by template args and `(` as functional-style cast (constructor call creating temporary object).

**Error Message (before fix):**
```
/usr/include/c++/14/bits/functional_hash.h:72:26: error: Expected template argument pattern in partial specialization
      struct __poison_hash<_Tp, __void_t<decltype(hash<_Tp>()(declval<_Tp>()))>>
                           ^
```

**Problematic Code Pattern:**
```cpp
template<typename _Tp, typename = void>
  struct __poison_hash { };

// Partial specialization with complex decltype:
template<typename _Tp>
  struct __poison_hash<_Tp, __void_t<decltype(hash<_Tp>()(declval<_Tp>()))>>
  { /* ... */ };
```

**Analysis:**
- The pattern involves: `decltype(hash<_Tp>()(declval<_Tp>()))`
- This is a decltype of a function call expression: `hash<_Tp>()(declval<_Tp>())`
- Which consists of: instantiate hash<_Tp>, construct an instance, call operator() with declval<_Tp>() as argument
- Parser needs to handle this complex nested expression as a template argument in partial specialization

**Investigation Update (2026-01-21):**
Root cause identified - the parser fails at step 2 (construct an instance) because:
1. After parsing `hash<_Tp>`, the parser sees `(`
2. `hash` is not recognized as a class template in this context, so `hash<_Tp>()` is not recognized as a functional-style cast / temporary object creation
3. Parser emits "Missing identifier: hash" error because it doesn't find `hash` as a function
4. This causes template argument parsing to fail with "Expected template argument pattern"

The fix requires: when parsing primary expressions in template contexts, after seeing `identifier<args>(`, recognize this as a functional-style cast (temporary object creation) if the identifier resolves to a class template, not just for function templates.

**Affected Headers:** `<variant>` (stops at line 72), potentially `<functional>`, `<optional>`, and others using hash-based SFINAE

**Key Finding:** Many patterns previously thought to be blockers actually **work correctly**:

✅ **Verified Working Patterns:**
- **Variadic non-type template params**: `template<size_t... _Indexes>` - Compiles successfully
  - Test case: `tests/test_variadic_nontype.cpp` 
- **Template alias with complex defaults**: Patterns like `typename _Up = typename remove_cv<_Tp>::type` work in isolation
  - Test case: `tests/test_utility_parse_error.cpp`

❌ **Actual Blockers:**
- **Logging Bug (FIXED 2026-01-21)**: Headers appeared to timeout not due to parse errors but due to logging bug where log arguments were evaluated even when filtered. With fix, headers compile in 8-11 seconds.
  - `<type_traits>` now compiles successfully
  - Complex headers like `<utility>`, `<functional>`, `<chrono>` still timeout due to template complexity (performance issue, not correctness)
- **Complex decltype in partial spec**: `__void_t<decltype(hash<T>()(...))>` still needs investigation
  - Test cases: `tests/test_just_type_traits.cpp` (was timing out, now works)
- **Context-dependent issues**: Parse errors occur only after including certain headers, suggesting parser state issues
  - Test case: `tests/test_utility_with_context.cpp`

#### 2.4 Variable Template Partial Specialization Pattern Matching (PARTIALLY FIXED - 2026-01-21)

**Previous Issue:** Variable template partial specializations like `__is_ratio_v<ratio<_Num, _Den>>` were registered without the base template name in the pattern, causing lookup failures.

**Fix Applied (Phase 1):** 
1. Store the base type name in `dependent_name` field when parsing partial specialization patterns
2. Check if `dependent_name` refers to a known template when building pattern key
3. Include template name in pattern (e.g., `__is_ratio_v_ratio`) only for template instantiation patterns

**Fix Applied (Phase 2 - 2026-01-21 PM):**
1. Extended `TemplateParamSubstitution` to include type parameter mappings (not just non-type values)
2. Register type substitutions during function template body re-parsing
3. Added substitution lookup in `try_instantiate_variable_template` to resolve template parameters
4. Enables `__is_ratio_v<_R1>` inside function templates to correctly substitute `_R1` with concrete types

**Example that NOW works:**
```cpp
template<typename _Tp>
constexpr bool __is_ratio_v = false;

template<long _Num, long _Den>
constexpr bool __is_ratio_v<ratio<_Num, _Den>> = true;

// Direct use works:
static_assert(__is_ratio_v<ratio<1,2>> == true);  // ✅ Works

// Simple function template returning variable template value:
template<typename _R>
constexpr bool is_ratio_check() { return __is_ratio_v<_R>; }  // ✅ Works
```

**Remaining Issue:** The `<ratio>` header still crashes during codegen because:
1. `if constexpr` evaluation with variable templates isn't fully working
2. The function `__are_both_ratios` uses nested `if constexpr` statements
3. The variable template identifier `__is_ratio_v` is not found in symbol table during code generation

**Affected Headers:** `<ratio>` (crashes during codegen in `__are_both_ratios`)

#### 2.4 Base Class Namespace Resolution (ACTIVE BLOCKER - 2026-01-21)

**Issue:** After fixing blocker #7.3, `<variant>` header progresses past line 72 but fails at line 299 with "Base class 'std' not found".

**Error Message:**
```
/usr/include/c++/14/bits/functional_hash.h:299:58: error: Base class 'std' not found
      struct __is_fast_hash<hash<long double>> : public std::false_type
                                                           ^
```

**Analysis:** The base class `std::false_type` is not being resolved correctly. This appears to be a namespace resolution issue where `std` is not recognized as a namespace in the base class specification.

**Affected Headers:** `<variant>`, potentially `<functional>`, `<optional>` and others that use `std::true_type` / `std::false_type` as base classes.

#### 3.4 Memory Corruption / bad_any_cast During Template Instantiation (**FIXED** - 2026-01-22)

**Issue:** Several headers crashed with SIGSEGV or `std::bad_any_cast` around 400-500 template instantiations.

**Root Causes Found and Fixed:**

1. **SIGSEGV (Fixed Previously):** The selective erase loop in `restore_token_position()` accessed potentially corrupted `ASTNode` objects.
   - **Fix:** Move discarded nodes to `ast_discarded_nodes_` vector instead of erasing them.

2. **std::bad_any_cast (Fixed 2026-01-22):** Member functions stored as `ASTNode` could be either `FunctionDeclarationNode` or `TemplateFunctionDeclarationNode`, but code called `.as<FunctionDeclarationNode>()` without checking.
   - **Locations Fixed:**
     - `Parser.cpp:26581` in `parse_template_declaration`
     - `SymbolTable.h` insert function
     - `AstNodeTypes.cpp` `findCopyAssignmentOperator` and `findMoveAssignmentOperator`
   - **Fix:** Added helper functions `is_function_or_template_function()` and `get_function_decl_node()` to safely handle both node types.

**Current Status:**
- `<variant>` - ❌ Parse Error (static_assert constexpr evaluation issue) - **SIGSEGV FIXED**
- `<functional>` - ❌ Parse Error (complex partial specialization patterns) - **bad_any_cast FIXED**

#### 3.5 decltype(auto) Return Type (**FIXED** - 2026-01-22)

**Issue:** `decltype(auto)` as a return type specifier was not recognized.

**Error Message (before fix):**
```
error: Expected primary expression
  static constexpr decltype(auto)
                           ^
```

**Fix Applied:** Added special case in `parse_decltype_specifier()` to handle `decltype(auto)` - when `auto` immediately follows the opening parenthesis, treat it as a C++14 deduced return type.

**Affected Headers:** `<functional>` (uses `decltype(auto)` in comparison operators)

#### 3.6 Nested Struct/Template with Base Class in Template Body (**FIXED** - 2026-01-22)

**Issue:** Nested structs and member templates with base classes inside template class bodies failed to parse.

**Error Messages (before fix):**
```
error: Expected '{' to start struct body
  struct __not_overloaded2 : true_type { };
                            ^
```

**Root Causes:**
1. `parse_struct_declaration()` only checked for `{` or `;` after struct name, not `:` (base class indicator)
2. `parse_member_struct_template()` didn't handle base classes for non-partial-specialization templates

**Fixes Applied:**
1. Added `:` to the condition checking for nested struct declarations (line 5563)
2. Added base class skipping logic in `parse_member_struct_template()` for primary templates (line ~30095)

**Patterns that now work:**
```cpp
template<typename T>
struct Outer {
    struct Inner : Base { };  // Fixed
    template<typename U> struct MemberTemplate : Base { };  // Fixed
};
```

#### 3.7 Type Alias Static Member Lookup in Constexpr (**FIXED** - 2026-01-22)

**Issue:** Static assertions like `static_assert(my_true::value, "...")` failed when `my_true` is a type alias to a template instantiation like `integral_constant<bool, true>`.

**Root Cause:** The constexpr evaluator couldn't:
1. Resolve type aliases to their underlying struct types
2. Follow the `type_index_` chain to find actual `StructTypeInfo` 
3. Trigger lazy static member instantiation for template-instantiated static members

**Fix Applied:** Enhanced `evaluate_qualified_identifier()` in ConstExprEvaluator.h:
1. Follow type alias chains even when `isStruct()` is true but `getStructInfo()` is null
2. Trigger lazy static member instantiation via `instantiateLazyStaticMember()` when needed
3. Re-lookup static member after instantiation to get the substituted initializer

**Note:** This fix works for global type aliases like `using my_true = integral_constant<bool, true>`. Local type aliases inside template class bodies (like in `<variant>`'s `parse_numbers.h`) still need additional work.

### 4. Missing pthread Types (**ACTIVE BLOCKER** - 2026-01-22)

**Issue:** Headers that depend on pthreads fail because `pthread_t` and related types are not defined.

**Error Message:**
```
/usr/include/pthread.h:205:37: error: Missing identifier
extern int pthread_create (pthread_t * __newthread, ...
                           ^~~~~~~~~
```

**Affected Headers:** `<atomic>`, `<barrier>`

**Root Cause:** FlashCpp doesn't parse the `bits/pthreadtypes.h` header correctly, or the header is not being included. This affects headers that use threading primitives.

### 5. Missing Internal Include Files (**ACTIVE BLOCKER** - 2026-01-22)

**Issue:** Some headers fail because internal GCC/libstdc++ headers cannot be found.

**Missing Files:**
- `execution_defs.h` - Required by `<memory>`, `<algorithm>`
- `unicode-data.h` - Required by `<chrono>`

**Root Cause:** These are internal implementation headers that may be in non-standard paths or require specific GCC version configuration.

### 6. `<compare>` Header Out-of-Line Static Constexpr Definition (**FIXED** - 2026-01-22 This PR)

**Status:** **FIXED** - The parsing issues have been resolved. The header now fails at a semantic level (function lookup for `strong_order`).

**Previous Issue:** The `<compare>` header failed to parse at line 153 with out-of-line static constexpr member definitions.

**Previous Error Message:**
```
/usr/include/c++/14/compare:153:25: error: Unexpected token
    partial_ordering::less(__cmp_cat::_Ord::less);
                          ^
```

**Current Error Message:** (now at line 621, semantic error)
```
/usr/include/c++/14/compare:621:31: error: No matching function for call to 'strong_order'
    strong_ordering(strong_order(static_cast<_Tp&&>(__t), static_cast<_Up&&>(__u)));
                                ^
```

**Fixes Applied:**
1. **Out-of-line static constexpr member definitions** - Parser now recognizes `ClassName::member(initializer)` syntax for static constexpr members
2. **`[[nodiscard]]` attributes in struct bodies** - Added attribute skipping at start of struct body member parsing
3. **Free-standing operator call syntax** - `operator<=>(args)` now works in requires expressions
4. **Member operator call syntax** - `obj.operator<=>(args)` now works
5. **Postfix operators after casts** - `static_cast<T>(x).operator<=>()` now works
6. **`operator() [[nodiscard]] (...)` attribute placement** - Attributes between operator name and parameter list now handled

**Remaining Issue:** The header now parses successfully but fails at semantic analysis (function lookup for `strong_order`). This is a semantic error, not a parse error.

**Affected Headers:** Most C++20 standard headers include `<compare>` directly or indirectly:
- `<vector>`, `<tuple>`, `<string>`, `<string_view>`, `<array>`
- `<map>`, `<set>`, `<span>`, `<ranges>`, `<iostream>`
- `<utility>`, `<bit>`, `<typeindex>`, `<coroutine>`

### 7. Nested `requires requires` Constraints (**FIXED** - 2026-01-23)

**Status:** **FIXED** in this PR

The `requires requires` syntax now works correctly. The previous issue was actually caused by partial specialization forward declarations not being handled properly, which caused the parser to think it was inside a struct body when it wasn't.

**Problematic Code Pattern (now works):**
```cpp
static pointer pointer_to(element_type& __r)
requires requires {
    { pointer::pointer_to(__r) } -> convertible_to<pointer>;
}
{ return pointer::pointer_to(__r); }
```

**Fixes Applied:**
- Added handling for partial specialization forward declarations (`template<typename T> struct X<T*>;`)
- Added qualified concept lookup for namespaced concepts (`std::same_as<T, U>`)

### 8. Parenthesized Concept Expressions in Constraints (**FIXED** - 2026-01-23)

**Status:** **FIXED** in this PR

Parenthesized concept expressions are now correctly recognized as concepts rather than C-style casts.

**Problematic Code Pattern (now works):**
```cpp
template<typename _Tp>
concept __adl_imove
  = (std::__detail::__class_or_enum<remove_reference_t<_Tp>>)  // parenthesized concept - now works
  && requires(_Tp&& __t) { iter_move(static_cast<_Tp&&>(__t)); };
```

**Fix Applied:** Added qualified name tracking during C-style cast detection to check if the "type" is actually a concept.

### 9. Template Instantiation Performance (NO LONGER A BLOCKER)

**Status:** The timeout issues have been **FIXED**. All headers now complete in 100-200ms.

Previously, headers appeared to timeout after 400-500 templates. This was actually caused by the infinite loop bug (see Blocker #0), not template instantiation performance.

**Current Performance (from progress logs):**
- Template cache hit rate: ~65-70%
- Average instantiation time: 8-10μs
- Peak instantiation time: up to 800μs for complex templates
- Standard headers complete in 100-200ms (no more timeouts)

**Optimization opportunities (for future work):**
- Improve template cache hit rate
- Optimize string operations in template name generation
- Consider lazy evaluation strategies
- Implement lazy instantiation for static members and whole template classes (see `docs/LAZY_TEMPLATE_INSTANTIATION_PLAN.md`)

### 10. std::initializer_list Compiler Magic (Known Limitation)

**Issue:** `std::initializer_list<T>` requires special compiler support that is not yet implemented.

**Example that fails:**
```cpp
#include <initializer_list>
int main() {
    std::initializer_list<int> list = {1, 2, 3};  // Error: Too many initializers
    return 0;
}
```

**Using initializer_list as constructor argument also fails:**
```cpp
#include <initializer_list>

class Container {
public:
    int sum;
    Container(std::initializer_list<int> list) : sum(0) {
        for (const int* it = list.begin(); it != list.end(); ++it) {
            sum += *it;
        }
    }
};

int main() {
    Container c{1, 2, 3};  // Error: "Too many initializers for struct"
    return c.sum;
}
```

**Root cause:** When parsing `Container c{1, 2, 3}`, FlashCpp attempts to match the 3 integer arguments to constructor parameters, but Container only has one constructor taking `std::initializer_list<int>`. The compiler magic needed would:
1. Detect that Container has an initializer_list constructor
2. Create a temporary array on the stack
3. Construct an `std::initializer_list` pointing to that array
4. Call the constructor with that initializer_list

**Expected behavior in standard C++:**
1. The compiler creates a temporary array `int __temp[] = {1, 2, 3}` on the stack
2. The compiler constructs `std::initializer_list` using its private constructor with a pointer to the array and size 3
3. This is compiler magic - the private constructor is only accessible to the compiler

**Current behavior in FlashCpp:**
- FlashCpp treats `std::initializer_list` like any other struct and tries aggregate initialization
- Since `std::initializer_list` only has 2 members (`_M_array` pointer and `_M_len` size), `{1, 2, 3}` fails with "too many initializers"
- For constructor calls, FlashCpp tries to match braced values to constructor parameters directly

**Workaround:** Use the default constructor and don't rely on brace-enclosed initializer lists for `std::initializer_list`:
```cpp
std::initializer_list<int> empty_list;  // Works - default constructor
std::initializer_list<int> two = {ptr, 2};  // Works - matches member count
```

**Note:** This is a fundamental limitation that affects many standard library patterns like range-based for loops with initializer lists (`for (int x : {1, 2, 3})`), container construction (`std::vector<int> v{1,2,3}`), and any class with an initializer_list constructor.

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
