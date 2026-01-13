# Standard Header Tests

This directory contains test files for C++ standard library headers to assess FlashCpp's compatibility with the C++ standard library.

## Current Status

| Header | Test File | Status | Blocker |
|--------|-----------|--------|---------|
| `<limits>` | `test_std_limits.cpp` | ✅ Compiled | - |
| `<type_traits>` | `test_std_type_traits.cpp` | ✅ Compiled | - |
| `<concepts>` | `test_std_concepts.cpp` | ✅ Compiled | - |
| `<utility>` | `test_std_utility.cpp` | ❌ Failed | `stl_pair.h` - dependent template type resolution |
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

### 1. `<utility>` - Dependent Template Type Resolution

**Location:** `stl_pair.h` - pattern `enable_if<__is_swappable<_Tp>::value>::type`

When parsing `enable_if<dependent_expr>::type`, the parser tries to resolve the `::type` member on a dependent template instantiation. This should be deferred until the template is actually instantiated with concrete types.

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
- Conversion operators (`operator T()`)
- Function pointer typedefs
- Variable templates with partial specializations
- Function reference/pointer types in template arguments

**Templates:**
- Fold expression evaluation in static members
- Namespace-qualified variable templates
- Member template requires clauses
- Template function `= delete`/`= default`

**C++20 Concepts:**
- Compound requirement noexcept specifier
- Template parameter brace initialization
- Globally qualified `::new`/`::delete`

**Other:**
- Named anonymous unions in typedef structs
- Direct initialization with `*this`
- Global scope `operator new`/`operator delete`

## See Also

- [`STANDARD_HEADERS_MISSING_FEATURES.md`](./STANDARD_HEADERS_MISSING_FEATURES.md) - Detailed analysis of missing features and implementation history
