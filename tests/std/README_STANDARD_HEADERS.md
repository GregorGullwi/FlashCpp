# Standard Header Tests

This directory contains test files for C++ standard library headers to assess FlashCpp's compatibility with the C++ standard library.

## Current Status

| Header | Test File | Status | Blocker |
|--------|-----------|--------|---------|
| `<limits>` | `test_std_limits.cpp` | ✅ Compiled | - |
| `<type_traits>` | `test_std_type_traits.cpp` | ✅ Compiled | - |
| `<concepts>` | `test_std_concepts.cpp` | ✅ Compiled | - |
| `<utility>` | `test_std_utility.cpp` | ❌ Failed | `c++config.h` - variable template brace initialization |
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

### 1. `<utility>` - Template Friend Declarations

**Location:** `bits/stl_pair.h` - pattern `template<typename _T1, typename _T2> friend struct pair;`

Template friend declarations are not fully supported. The parser fails when parsing a friend declaration of a class template inside another template class.

**Example pattern:**
```cpp
template<typename _T1, typename _T2>
struct pair {
    template<typename _U1, typename _U2> friend struct pair;  // Template friend declaration
};
```

**Previous blockers resolved (January 13, 2026):**
- Variable template brace initialization: Pattern `inline constexpr in_place_type_t<_Tp> in_place_type{};` now works
- C++17 nested namespaces: Pattern `namespace A::B::C { }` now works
- `const typename` in type specifiers: Pattern `constexpr const typename T::type` now works

### 2. Template Instantiation Performance

Most headers timeout due to template instantiation volume, not parsing errors. Individual instantiations are fast (20-50μs), but standard headers trigger thousands of instantiations.

**Optimization opportunities:**
- Improve template cache hit rate (currently ~26%)
- Optimize string operations in template name generation
- Consider lazy evaluation strategies

### 3. Missing Infrastructure

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
- Member template requires clauses
- Template function `= delete`/`= default`

**C++17/C++20 Features:**
- C++17 nested namespace declarations (`namespace A::B::C { }`)
- Compound requirement noexcept specifier
- Template parameter brace initialization
- Globally qualified `::new`/`::delete`

**Other:**
- Named anonymous unions in typedef structs
- Direct initialization with `*this`
- Global scope `operator new`/`operator delete`

## Recent Changes

### 2026-01-13: Multiple Parsing Improvements

**Fixed:** Three parsing issues that were blocking `<utility>` header progress:

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

#### 3. `const typename` in Type Specifiers

- Pattern: `constexpr const typename tuple_element<...>::type&`
- Previously: Parser didn't recognize `typename` after `const`
- Now: `typename` is recognized after cv-qualifiers

**Progress:** `<utility>` parsing now advances to `stl_pair.h` template friend declarations.

### 2026-01-13: Library Type Traits vs Compiler Intrinsics

**Fixed:** Identifiers starting with `__is_` or `__has_` followed by `<` are now correctly treated as library type traits (template classes) rather than compiler intrinsics.

- `__is_swappable<T>` → Treated as template class (library type trait)
- `__is_void(T)` → Treated as compiler intrinsic

## See Also

- [`STANDARD_HEADERS_MISSING_FEATURES.md`](./STANDARD_HEADERS_MISSING_FEATURES.md) - Detailed analysis of missing features and implementation history
