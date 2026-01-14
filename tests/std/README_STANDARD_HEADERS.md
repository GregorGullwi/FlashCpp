# Standard Header Tests

This directory contains test files for C++ standard library headers to assess FlashCpp's compatibility with the C++ standard library.

## Current Status

| Header | Test File | Status | Blocker |
|--------|-----------|--------|---------|
| `<limits>` | `test_std_limits.cpp` | ✅ Compiled | - |
| `<type_traits>` | `test_std_type_traits.cpp` | ✅ Compiled | - |
| `<concepts>` | `test_std_concepts.cpp` | ✅ Compiled | - |
| `<utility>` | `test_std_utility.cpp` | ⏱️ Timeout | Parsing fixed; template volume |
| `<string_view>` | `test_std_string_view.cpp` | ⏱️ Timeout | Template instantiation volume |
| `<string>` | `test_std_string.cpp` | ⏱️ Timeout | Allocators, exceptions |
| `<vector>` | `test_std_vector.cpp` | ⏱️ Timeout | Template instantiation volume |
| `<array>` | `test_std_array.cpp` | ⏱️ Timeout | Template instantiation volume |
| `<tuple>` | `test_std_tuple.cpp` | ⏱️ Timeout | Variadic templates |
| `<optional>` | `test_std_optional.cpp` | ⏱️ Timeout | Template instantiation volume |
| `<variant>` | `test_std_variant.cpp` | ⏱️ Timeout | Template instantiation volume |
| `<memory>` | `test_std_memory.cpp` | ⏱️ Timeout | Smart pointers, allocators |
| `<functional>` | `test_std_functional.cpp` | ⏱️ Timeout | std::function, type erasure |
| `<algorithm>` | `test_std_algorithm.cpp` | ⏱️ Timeout | Template instantiation volume |
| `<map>` | `test_std_map.cpp` | ⏱️ Timeout | Template instantiation volume |
| `<set>` | `test_std_set.cpp` | ⏱️ Timeout | Template instantiation volume |
| `<span>` | `test_std_span.cpp` | ⏱️ Timeout | Template instantiation volume |
| `<any>` | `test_std_any.cpp` | ⏱️ Timeout | Type erasure, RTTI |
| `<ranges>` | `test_std_ranges.cpp` | ⏱️ Timeout | Concepts, views |
| `<iostream>` | `test_std_iostream.cpp` | ⏱️ Timeout | Virtual inheritance, locales |
| `<chrono>` | `test_std_chrono.cpp` | ⏱️ Timeout | Ratio templates |

**Legend:** ✅ Compiled | ❌ Failed | ⏱️ Timeout (>10s)

### C Library Wrappers (Also Working)

| Header | Test File | Notes |
|--------|-----------|-------|
| `<cstddef>` | `test_cstddef.cpp` | `size_t`, `ptrdiff_t`, `nullptr_t` |
| `<cstdlib>` | `test_cstdlib.cpp` | `malloc`, `free`, etc. |
| `<cstdio>` | `test_cstdio_puts.cpp` | `printf`, `puts`, etc. |

## Running the Tests

```bash
cd tests/std
./test_std_headers_comprehensive.sh
```

## Current Blockers

### 1. Template Instantiation Performance

Most headers timeout due to template instantiation volume, not parsing errors. Individual instantiations are fast (20-50μs), but standard headers trigger thousands of instantiations.

**Optimization opportunities:**
- Improve template cache hit rate (currently ~26%)
- Optimize string operations in template name generation
- Consider lazy evaluation strategies

### 2. Template Name Resolution in std Namespace

**Location:** `<type_traits>` header - Variable template lookups during template alias resolution

The compiler encounters issues when resolving variable template names during parsing of the standard library headers. Variable templates like `is_reference_v` are registered with qualified names (`std::is_reference_v`) but some internal lookups use unqualified names.

**Example pattern:**
```cpp
// In std::, this pattern requires proper namespace resolution:
template <typename _Tp>
  inline constexpr bool is_reference_v = __is_reference(_Tp);

// When used inside template aliases:
template<typename _Xp, typename _Yp>
  requires is_reference_v<__condres_cvref<_Xp, _Yp>>  // lookup fails for 'is_reference_v'
```

**Error:** `No primary template found for 'is_reference_v'`

**Root cause:** The `lookupTemplate` function is called with unqualified name `is_reference_v` but the template is registered with qualified name `std::is_reference_v`.

**Previous blockers resolved (January 14, 2026):**
- Template member constructors: Pattern `template<typename U> Box(const Box<U>& other)` now parses correctly
  - **Fix:** Added template constructor detection in `parse_member_function_template()` before calling `parse_template_function_declaration_body()`
  - **Test case:** `tests/test_template_ctor_ret0.cpp`
- Namespace-qualified variable template lookup: Variable templates in namespaces can now be used from function templates in the same namespace
  - **Fix:** Added namespace-qualified lookup in multiple code paths for variable templates in `parse_unary_expression()`
  - **Test case:** `tests/test_ns_var_template_ret0.cpp`
- Non-type template parameters in return types: Pattern `typename tuple_element<_Int, pair<_Tp1, _Tp2>>::type&` now works
  - **Fix:** Set `current_template_param_names_` EARLY in `parse_template_declaration()`, before variable template detection code calls `parse_type_specifier()`
  - **Test case:** `tests/test_nontype_template_param_return_ret0.cpp`

**Previous blockers resolved (January 13, 2026):**
- Member template function calls: Pattern `Helper<int>::Check<int>()` now works
- Template friend declarations: Pattern `template<typename _U1, typename _U2> friend struct pair;` now works
- Variable template brace initialization: Pattern `inline constexpr in_place_type_t<_Tp> in_place_type{};` now works
- C++17 nested namespaces: Pattern `namespace A::B::C { }` now works
- C++20 inline nested namespaces: Pattern `namespace A::inline B { }` now works
- `const typename` in type specifiers: Pattern `constexpr const typename T::type` now works

### 3. Template Argument Reference Preservation

**Issue:** Template argument substitution can lose reference qualifiers when substituting type parameters.

**Example pattern:**
```cpp
template<typename T>
constexpr bool test() {
    return is_reference_v<T>;  // T=int& becomes just int
}

test<int&>();  // Should return true, but returns false
```

**Root cause:** When instantiating function templates, reference qualifiers on template arguments are not properly preserved when passed to nested variable templates.

### 3. Template Instantiation Performance

Most headers timeout due to template instantiation volume, not parsing errors. Individual instantiations are fast (20-50μs), but standard headers trigger thousands of instantiations.

**Optimization opportunities:**
- Improve template cache hit rate (currently ~26%)
- Optimize string operations in template name generation
- Consider lazy evaluation strategies

### 4. Missing Infrastructure

- **Exception handling** - Required for containers (`<vector>`, `<string>`)
- **Allocator support** - Required for `<vector>`, `<string>`, `<map>`, `<set>`
- **Locales** - Required for `<iostream>`

## Adding New Standard Header Tests

1. Create `test_std_<header>.cpp`:
   ```cpp
   #include <header>
   int main() { return 0; }
   ```

2. Verify valid C++20: `clang++ -std=c++20 -c test_std_<header>.cpp`

3. Test: `./test_std_headers_comprehensive.sh`

4. If it fails, add to `EXPECTED_FAIL` in `../run_all_tests.sh`

## Implemented Features Summary

The following features have been implemented to support standard headers:

**Preprocessor:**
- Multiline macro invocations (macro arguments spanning multiple lines)
- Angle bracket protection in variadic macro arguments (commas inside `<>` are preserved)

**Type System:**
- Type traits intrinsics (`__is_same`, `__is_class`, `__is_pod`, etc.)
- Library type traits vs. intrinsic disambiguation (`__is_swappable<T>` vs `__is_void(T)`)
- Conversion operators (`operator T()`)
- Function pointer typedefs
- Variable templates with partial specializations
- Variable templates with brace initialization (`constexpr Type<T> name{};`)
- Function reference/pointer types in template arguments

**Templates:**
- Fold expression evaluation in static members
- Namespace-qualified variable templates
- Namespace-qualified variable template lookup in function templates
- Member template requires clauses
- Template function `= delete`/`= default`
- Template friend declarations (`template<typename T1, typename T2> friend struct pair;`)
- Non-type template parameters in return types (`typename tuple_element<_Int, pair<_Tp1, _Tp2>>::type&`)
- Template member constructors (`template<typename U> Box(const Box<U>& other)`) (NEW)
- Template alias declarations with requires clauses (NEW)

**C++17/C++20 Features:**
- C++17 nested namespace declarations (`namespace A::B::C { }`)
- C++20 inline nested namespace declarations (`namespace A::inline B { }`)
- Compound requirement noexcept specifier
- Template parameter brace initialization
- Globally qualified `::new`/`::delete`
- Template alias declarations with requires clauses (`template<typename T> requires Constraint<T> using Alias = T;`)

**Other:**
- Named anonymous unions in typedef structs
- Direct initialization with `*this`
- Global scope `operator new`/`operator delete`

## Recent Changes

### 2026-01-14: Template Alias with Requires Clause

**Fixed:** Template alias declarations with requires clauses can now be parsed correctly (both global and member aliases).

- Pattern: `template<typename T> requires is_reference_v<T> using RefType = T;`
- Previously: Parser didn't recognize `using` keyword after requires clause
- Now: Both global `parse_template_declaration()` and member `parse_member_template_or_function()` properly detect and parse requires clauses before `using`
- **Test case:** `tests/test_requires_clause_alias_ret0.cpp`

**Example:**
```cpp
namespace ns {
    template<typename _Tp>
    inline constexpr bool is_reference_v = false;
    
    template<typename _Tp>
    inline constexpr bool is_reference_v<_Tp&> = true;
    
    // Global template alias with requires clause
    template<typename _Xp, typename _Yp>
        requires is_reference_v<_Xp>  
    using CondresRef = _Xp;
    
    // Member template alias with requires clause
    template<typename T>
    struct Test {
        template<typename U>
            requires is_reference_v<U>
        using ValueType = U;
    };
}
```

**Technical details:**
- Added re-check for `is_alias_template` after parsing requires clause in `parse_template_declaration()` (line 22880)
- Extended lookahead in `parse_member_template_or_function()` to skip requires clause expressions before checking for `using` keyword
- Added requires clause parsing to `parse_member_template_alias()` with proper template parameter context setup

**Progress:** Template aliases with requires clauses (like those in `<type_traits>` for `__condres_cvref`) now parse successfully.

### 2026-01-14: Template Member Constructor Parsing

**Fixed:** Template member constructors can now be parsed correctly.

- Pattern: `template<typename U> Box(const Box<U>& other) : value(other.value) {}`
- Previously: Parser tried to parse `Box(` as a return type and function name, failing with "Expected identifier token" when it encountered `(` after the constructor name
- Now: `parse_member_function_template()` detects template constructor patterns before calling `parse_template_function_declaration_body()`
- **Test case:** `tests/test_template_ctor_ret0.cpp`

**Example:**
```cpp
template<typename T>
struct Box {
    T value;
    
    // Template constructor - now parses correctly
    template<typename U>
    Box(const Box<U>& other) : value(other.value) {}
};
```

**Technical details:**
- Added lookahead in `parse_member_function_template()` after parsing template parameters and requires clause
- Skip storage specifiers (constexpr, explicit, inline) and check if next identifier matches the struct name followed by `(`
- If detected, parse as a constructor with `ConstructorDeclarationNode` instead of routing to `parse_template_function_declaration_body()`
- Handles initializer lists, noexcept specifier, `= default`, `= delete`, and function bodies

**Progress:** Template constructor declarations now parse successfully. The `<utility>` header progresses to the template instantiation phase before timing out.

### 2026-01-14: Namespace-Qualified Variable Template Lookup

**Fixed:** Variable templates defined in namespaces can now be used from within function templates in the same namespace.

- Pattern: `namespace ns { template<typename T> constexpr bool is_foo_v = ...; template<typename T> bool test() { return is_foo_v<T>; } }`
- Previously: Parser failed with "Missing identifier: is_foo_v" when the variable template was registered with qualified name `ns::is_foo_v` but looked up with unqualified name
- Now: Multiple code paths try namespace-qualified lookup when unqualified lookup fails
- **Test case:** `tests/test_ns_var_template_ret0.cpp`

**Example:**
```cpp
namespace ns {
    template<typename _Tp>
    inline constexpr bool is_simple_v = true;
    
    template<typename T>
    constexpr bool test() {
        return is_simple_v<T>;  // Now correctly resolves to ns::is_simple_v
    }
}
```

**Technical details:** 
- Added namespace-qualified lookup in `parse_unary_expression()` after template argument parsing
- When `gTemplateRegistry.lookupVariableTemplate(name)` fails, now tries `buildQualifiedName(current_ns_path, name)` and looks up again
- This fix applies to variable templates used inside function templates where the variable template is in the same namespace

**Remaining issue:** Full std library parsing still fails due to similar namespace resolution issues during template alias evaluation.

### 2026-01-14: Non-Type Template Parameters in Return Types

**Fixed:** Non-type template parameters are now properly recognized in complex return types.

- Pattern: `template<size_t _Int, typename _Tp1, typename _Tp2> typename tuple_element<_Int, pair<_Tp1, _Tp2>>::type&`
- Previously: Parser failed with "Missing identifier: _Int" because `current_template_param_names_` was not set before parsing the return type during variable template detection
- Now: Template parameters (including non-type parameters like `size_t _Int`) are properly recognized in return type expressions
- **Test case:** `tests/test_nontype_template_param_return_ret0.cpp`

**Example:**
```cpp
template<int _Int, typename _Tp1, typename _Tp2>
constexpr typename element_type<_Int, _Tp1>::type
get_element();  // _Int is now recognized in the return type
```

**Technical details:** 
- The fix moves the assignment `current_template_param_names_ = template_param_names;` to an earlier position in `parse_template_declaration()`
- This ensures template parameters are available before the variable template detection code (which calls `parse_type_specifier()`)
- Also set `parsing_template_body_ = true` at the same point

**Progress:** `<utility>` parsing now advances past the `tuple_element<_Int, ...>` return type patterns to constructor declaration parsing.

### 2026-01-13: Member Template Function Calls

**Fixed:** Calling member template functions with explicit template arguments now works.

- Pattern: `Helper<T>::Check<U>()` - calling a member template function with explicit template args
- Previously: Parser failed after `Check` when encountering `<U>()` because it didn't recognize member template function call syntax
- Now: Member template functions can be called with explicit template arguments
- **Test case:** `tests/test_member_template_call_ret0.cpp` (added)

**Example:**
```cpp
template<typename T>
struct Helper {
    template<typename U>
    static constexpr bool Check() { return true; }
};

int main() {
    bool x = Helper<int>::Check<int>(); // Now works!
    return 0;
}
```

**Technical details:** Two code paths were updated:
1. In `parse_unary_expression`: After parsing `Template<T>::member`, check for `<` (member template args) and `(` (function call)
2. Same pattern in the earlier qualified identifier path at line 18166

### 2026-01-13: Template Friend Declarations

**Fixed:** Template friend declarations inside template classes are now supported.

- Pattern: `template<typename T1, typename T2> friend struct pair;`
- Previously: Parser failed with "Expected identifier token" when encountering `friend` after template parameters
- Now: Template friend declarations are properly detected and parsed
- **Test case:** `tests/test_template_friend_decl_ret0.cpp`

**Progress:** `<utility>` parsing now advances past template friend declarations to member template alias parsing at line 765.

### 2026-01-13: Multiple Parsing Improvements

**Fixed:** Four parsing issues that were blocking `<utility>` header progress:

#### 1. Variable Template Brace Initialization

- Pattern: `template<typename T> inline constexpr Type<T> name{};`
- Previously: Parser expected `()` after variable template name, failed on `{}`
- Now: Both `= value` and `{}` initialization are supported
- **Test case:** `tests/test_var_template_brace_init_ret0.cpp`

#### 2. C++17 Nested Namespace Declarations

- Pattern: `namespace A::B::C { ... }`
- Previously: Parser expected `{` immediately after first namespace name
- Now: Multiple namespace names separated by `::` are supported
- **Test case:** `tests/test_nested_namespace_ret42.cpp`

#### 3. C++20 Inline Nested Namespace Declarations

- Pattern: `namespace A::inline B { ... }`
- Previously: Not supported
- Now: The `inline` keyword makes B's members visible in A without B:: prefix
- **Test case:** `tests/test_nested_namespace_ret42.cpp` (includes inline case)

#### 4. `const typename` in Type Specifiers

- Pattern: `constexpr const typename tuple_element<...>::type&`
- Previously: Parser didn't recognize `typename` after `const`
- Now: `typename` is recognized after cv-qualifiers

**Progress:** `<utility>` parsing advanced past variable template brace initialization and nested namespace issues.

### 2026-01-13: Library Type Traits vs Compiler Intrinsics

**Fixed:** Identifiers starting with `__is_` or `__has_` followed by `<` are now correctly treated as library type traits (template classes) rather than compiler intrinsics.

- `__is_swappable<T>` → Treated as template class (library type trait)
- `__is_void(T)` → Treated as compiler intrinsic

## See Also

- [`STANDARD_HEADERS_MISSING_FEATURES.md`](./STANDARD_HEADERS_MISSING_FEATURES.md) - Detailed analysis of missing features and implementation history
