# Standard Header Tests

This directory contains test files for C++ standard library headers to assess FlashCpp's compatibility with the C++ standard library.

## Test Files

### Standard Header Test Files (`test_std_*.cpp`)

These files test FlashCpp's ability to compile and use various C++ standard library headers:

| Header | Test File | Status | Notes |
|--------|-----------|--------|-------|
| `<type_traits>` | `test_std_type_traits.cpp` | ❌ Failed | Requires conversion operators |
| `<string_view>` | `test_std_string_view.cpp` | ⏱️ Timeout | Complex template instantiation |
| `<string>` | `test_std_string.cpp` | ⏱️ Timeout | Allocators, exceptions |
| `<iostream>` | `test_std_iostream.cpp` | ⏱️ Timeout | Virtual inheritance, locales |
| `<tuple>` | `test_std_tuple.cpp` | ⏱️ Timeout | Variadic templates, SFINAE |
| `<vector>` | `test_std_vector.cpp` | ⏱️ Timeout | Allocators, exceptions |
| `<array>` | `test_std_array.cpp` | ⏱️ Timeout | constexpr support |
| `<algorithm>` | `test_std_algorithm.cpp` | ⏱️ Timeout | Iterators, concepts |
| `<utility>` | `test_std_utility.cpp` | ⏱️ Timeout | std::pair, std::move (template volume) |
| `<memory>` | `test_std_memory.cpp` | ⏱️ Timeout | Smart pointers, allocators |
| `<functional>` | `test_std_functional.cpp` | ⏱️ Timeout | std::function, type erasure |
| `<map>` | `test_std_map.cpp` | ⏱️ Timeout | Red-black trees, allocators |
| `<set>` | `test_std_set.cpp` | ⏱️ Timeout | Red-black trees, allocators |
| `<optional>` | `test_std_optional.cpp` | ⏱️ Timeout | Union templates |
| `<variant>` | `test_std_variant.cpp` | ⏱️ Timeout | Advanced union handling |
| `<any>` | `test_std_any.cpp` | ❌ Failed | Type erasure, RTTI |
| `<span>` | `test_std_span.cpp` | ⏱️ Timeout | constexpr support |
| `<concepts>` | `test_std_concepts.cpp` | ⏱️ Timeout | Requires clauses work, but template volume causes timeout |
| `<ranges>` | `test_std_ranges.cpp` | ⏱️ Timeout | Concepts, views |
| `<limits>` | `test_std_limits.cpp` | ✅ Compiled | Successfully compiles in ~1.8s! |
| `<chrono>` | `test_std_chrono.cpp` | ⏱️ Timeout | Ratio templates, duration |

**Legend:**
- ❌ Failed: Compilation fails with errors
- ⏱️ Timeout: Compilation takes >10 seconds (likely hangs)
- ⚠️ Partial: Compiles but some features don't work correctly
- ✅ Compiled: Successfully compiles

## Running the Tests

### Comprehensive Test Script

Run all standard header tests systematically:

```bash
cd tests
./test_std_headers_comprehensive.sh
```

This script:
- Tests each standard header with a 10-second timeout
- Reports compilation success, timeout, or failure
- Shows first error message for failed tests
- Provides a summary of results

### Integration with Test Suite

The standard header tests are included in `run_all_tests.sh` but are in the `EXPECTED_FAIL` list since they currently don't compile. As features are implemented in FlashCpp, successfully compiling headers should be moved out of the exclusion list.

## Missing Features Analysis

See [`STANDARD_HEADERS_MISSING_FEATURES.md`](./STANDARD_HEADERS_MISSING_FEATURES.md) for a comprehensive analysis of:
- Why each header fails to compile
- What C++ features are missing in FlashCpp
- Priority of features for implementation
- Recommended implementation order
- Required compiler intrinsics

### Key Blockers

The main features preventing standard header compilation:

1. **Conversion operators** (`operator T()`) - ✅ Implemented
2. **Advanced constexpr support** - ⚠️ Partially implemented
3. **Template instantiation optimization** (causes timeouts)
4. **Type traits compiler intrinsics** (`__is_same`, etc.) - ✅ Implemented
5. **Exception handling infrastructure**
6. **Allocator support**

## Test File Characteristics

All test files:
- ✅ Are valid C++20 (verified with clang++ -std=c++20)
- ✅ Include a `main()` function
- ✅ Return 0 on success
- ✅ Use minimal code to test header inclusion
- ✅ Are self-contained (no external dependencies except standard library)

## Adding New Standard Header Tests

To add a new standard header test:

1. Create a test file `test_std_<header_name>.cpp`:
   ```cpp
   // Test standard <header_name> header
   #include <header_name>
   
   int main() {
       // Minimal usage of header features
       return 0;
   }
   ```

2. Verify it's valid C++20:
   ```bash
   clang++ -std=c++20 -c test_std_<header_name>.cpp
   ```

3. Test with FlashCpp:
   ```bash
   ./test_std_headers_comprehensive.sh
   ```

4. If it fails/timeouts, add to `EXPECTED_FAIL` in `run_all_tests.sh`:
   ```bash
   EXPECTED_FAIL=(
       ...
       "test_std_<header_name>.cpp"  # Reason for failure
   )
   ```

## Working Standard Headers

Currently, FlashCpp successfully compiles a few simpler standard C library wrappers:

- ✅ `<cstddef>` - Tested in `test_cstddef.cpp`
- ✅ `<cstdlib>` - Tested in `test_cstdlib.cpp`
- ✅ `<cstdio>` - Tested in `test_cstdio_puts.cpp`

These work because they're mostly C library wrappers with minimal C++ template complexity.

## Future Work

As FlashCpp gains more C++ features:
1. Re-run `test_std_headers_comprehensive.sh` regularly
2. Move successfully compiling tests out of `EXPECTED_FAIL`
3. Add return value verification tests (currently just testing compilation)
4. Add link and execution tests
5. Create more focused unit tests for specific standard library features

## Latest Investigation (January 10, 2026 - Nested Template Base Classes)

### ✅ IMPLEMENTED: Nested Template Base Classes in Partial Specializations

**Pattern Now Supported:** Partial specializations can now have base classes that use nested template instantiations with dependent arguments:
```cpp
template<typename Tp1, typename Tp2, typename... Rp>
struct common_type<Tp1, Tp2, Rp...>
    : public fold<common_type<Tp1, Tp2>, pack<Rp...>>
{ };
```

**Current Status:**
- ✅ Properly defers base class resolution when template arguments are dependent
- ✅ Creates dependent type placeholders for templates instantiated with dependent arguments
- ✅ Improved identifier matching to recognize template parameters in generated type names (e.g., `pack_Rp` contains `Rp`)
- ✅ Checks both `is_dependent` and `is_pack` flags when determining dependency

**Implementation Details:**
1. Modified partial specialization base class parsing to call `struct_ref.add_deferred_template_base_class()` instead of just skipping
2. Added check for `is_pack` when determining if template arguments are dependent
3. When `try_instantiate_class_template` returns nullopt due to dependent args, now creates a dependent type placeholder with a name that includes the template parameters
4. Improved `matches_identifier` function to recognize underscore as a valid separator in template type names

**Test Cases:**
- ✅ `tests/test_nested_template_base_ret0.cpp` - Compiles and runs successfully

**Impact:**
- ✅ `<type_traits>` now parses past line 2422!
- Previous blocker at line 2422 (`struct common_type<_Tp1, _Tp2, _Rp...> : public __common_type_fold<common_type<_Tp1, _Tp2>, __common_type_pack<_Rp...>>`) - **Fixed!**

## Latest Investigation (January 10, 2026 - __underlying_type Support)

### ✅ IMPLEMENTED: __underlying_type(T) as Type Specifier

**Pattern Now Supported:** The `__underlying_type(T)` intrinsic used in `<type_traits>` to get the underlying type of an enum:
```cpp
template<typename _Tp>
struct __underlying_type_impl
{
    using type = __underlying_type(_Tp);
};
```

**Current Status:**
- ✅ Added support for `__underlying_type(T)` in type specifier context
- ✅ Properly resolves to the underlying type for concrete enum types
- ✅ Returns dependent type placeholder for template parameters (resolved at instantiation)

**Test Cases:**
- ✅ `tests/test_underlying_type_ret42.cpp` - Compiles and runs successfully

**Impact:**
- ✅ `<type_traits>` now parses past line 2443 to line 2499!
- Previous blocker at line 2443 (`using type = __underlying_type(_Tp);`) - **Fixed!**

### Current Blocker (Line 2499)

**Error:** `Expected identifier token`

**Pattern:**
```cpp
template<typename _Fp, typename _Tp1, typename... _Args>
  static __result_of_success<decltype(
  (std::declval<_Tp1>().*std::declval<_Fp>())(std::declval<_Args>()...)
  ), __invoke_memfun_ref> _S_test(int);
```

**Why It Fails:**
- This is a complex pattern using pointer-to-member operator (`.*`)
- The decltype expression involves calling a member function pointer on a declval result
- This requires support for pointer-to-member dereference operators in expression parsing

**Next Steps:**
- Implement pointer-to-member operators (`.*` and `->*`) in expression parsing
- Handle member function pointer calls through declval

## Previous Investigation (January 10, 2026 - Decltype Improvements)

### ✅ IMPLEMENTED: TernaryOperatorNode Type Deduction in decltype

**Pattern Now Supported:** The pattern `decltype(true ? expr1 : expr2)` used in `<type_traits>` for common_type computation:
```cpp
template<typename T, typename U>
using cond_t = decltype(true ? std::declval<T>() : std::declval<U>());
```

**Current Status:**
- ✅ TernaryOperatorNode handling added to `get_expression_type()` function
- ✅ Returns common type of both branches when possible
- ✅ Falls back to Auto type in template contexts for dependent expressions

**Test Cases:**
- ✅ `tests/test_decltype_ternary_common_type_ret0.cpp` - Compiles successfully

### ✅ IMPLEMENTED: Static Decltype Return Types

**Pattern Now Supported:** `static decltype(expr)` as function return type in template member functions:
```cpp
template<typename T, typename U>
static decltype(_S_test<T, U>(0)) func(...);
```

**Current Status:**
- ✅ Added check for `decltype` after function specifiers in `parse_type_specifier()`
- ✅ Properly handles `static decltype(...)`, `inline decltype(...)`, etc.

**Test Cases:**
- ✅ `tests/test_static_decltype_return_ret0.cpp` - Compiles successfully

### ✅ IMPLEMENTED: Template Parameter Context for Member Function Templates

**Issue Fixed:** `current_template_param_names_` was empty when parsing decltype in member function template declarations.

**Solution:** Added code to set up template parameter names in `parse_member_function_template()` before calling `parse_template_function_declaration_body()`.

### ✅ IMPLEMENTED: Struct Parsing Context for Partial Specializations

**Issue Fixed:** `struct_parsing_context_stack_` was not set up for partial specializations, preventing inherited member template lookup.

**Solution:** Added `struct_parsing_context_stack_.push_back()` in partial specialization parsing to enable proper base class member lookup.

**Impact:**
- ✅ `<type_traits>` now parses from line 2351 to line 2422 (71 more lines!)
- Previous blocker at line 2351 (`decltype(true ? std::declval<_Tp>() : std::declval<_Up>())`) - Fixed
- Previous blocker at line 2372 (`static decltype(_S_test_2<_Tp, _Up>(0))`) - Fixed
- Previous blocker at line 2403 (`using type = decltype(_S_test<_Tp1, _Tp2>(0));` - inherited member lookup) - Fixed
- Previous blocker at line 2422 (`struct common_type<_Tp1, _Tp2, _Rp...> : __common_type_fold<...>`) - **Fixed January 10, 2026**

## Latest Investigation (January 9, 2026 - Named Anonymous Unions/Structs)

### ✅ IMPLEMENTED: Named Anonymous Struct/Union Pattern Support

**Pattern Now Supported:** Multiple standard library headers (`<type_traits>`, `<utility>`, `<cstdio>`) use the named anonymous struct/union pattern:
```cpp
struct Container {
    union {
        int i;
        float f;
    } data;  // ← Named anonymous union member - NOW WORKS!
};
```

**Current Status:**
- ✅ This pattern is **NOW FULLY SUPPORTED** (implemented in commits f86fce8, 44d188b, 25ce897)
- ✅ Creates implicit anonymous types during parsing
- ✅ Handles member access chains (e.g., `container.data.field`)
- **Distinction**: This is different from named union types with member names
  - ✅ `union Data { int i; } data;` - **WORKS** (named union type with member name) - Added in commit f0e5a18
  - ✅ `union { int i; } data;` - **NOW WORKS** (anonymous union type with member name) - Added in commits f86fce8-25ce897

**Previously Blocking Headers - Now Unblocked:**
- ✅ `/usr/include/c++/14/type_traits:2162` - `struct __attribute__((__aligned__)) { } __align;` - Parses successfully
- ✅ `/usr/include/x86_64-linux-gnu/bits/types/__mbstate_t.h:20` - `union { ... } __value;` - Parses successfully

**Implementation Details:**
1. ✅ Creates implicit anonymous struct/union types with unique generated names
2. ✅ Parses members directly into the anonymous type
3. ✅ Calculates proper layout (unions: overlapping, structs: sequential)
4. ✅ Supports multiple comma-separated declarators (e.g., `} a, b, c;`)
5. ✅ Uses `skip_balanced_braces()` for efficient peek-ahead detection

**Test Cases - All Passing:**
- ✅ `tests/test_named_anonymous_struct_ret42.cpp` - Returns 42 correctly
- ✅ `tests/test_named_anonymous_union_ret42.cpp` - Returns 42 correctly
- ✅ `tests/test_nested_anonymous_union_ret15.cpp` - Returns 15 correctly
- ✅ `tests/test_nested_union_ret0.cpp` - Returns 0 correctly


## Latest Investigation (January 8, 2026 - Afternoon)

### Key Findings

1. **Template instantiation volume remains the primary blocker**
   - Most headers that timeout are not due to missing features
   - Individual template instantiations are fast (~20-50μs)
   - Standard headers contain hundreds to thousands of template instantiations
   - This is a performance optimization issue, not a feature gap
   - Compile FlashCpp in release (-O3) mode when testing these features


### What Actually Works

Based on testing, the following features are confirmed working:

- ✅ C++20 requires clauses
- ✅ Basic C++20 concepts
- ✅ `<limits>` header with `numeric_limits<T>` specializations
- ✅ Template member functions with return value access
- ✅ Member access in regular template classes (non-union)
- ✅ Decltype with ternary operators
- ✅ Static constexpr members in template classes
- ✅ Floating-point arithmetic (multiply, divide, add, subtract)
- ✅ Union declarations (both named and anonymous)
- ✅ Anonymous unions in template classes (declaration only)

### What Doesn't Work

- ❌ **Accessing union members causes compilation to hang** (critical bug)
- ❌ Anonymous union member access in templates gives "Missing identifier" error
- ⏱️ Many headers timeout due to template instantiation volume

### Recommendations

1. **For immediate productivity**: Use `<limits>` header which now works!
2. **For optional-like types**: **Cannot use unions** - critical bug causes hangs
3. **For template performance**: Consider breaking up large template hierarchies or using explicit instantiations where possible
4. **Union support**: Avoid using unions with member access until bug is fixed

## Related Files

- `STANDARD_HEADERS_MISSING_FEATURES.md` - Detailed analysis of missing features
- `test_std_headers_comprehensive.sh` - Test runner script
- `run_all_tests.sh` - Main test suite (includes these tests in exclusion list)
- `test_real_std_headers_fail.cpp` - Earlier analysis of header support issues
