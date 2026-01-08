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

### Recent Fixes (January 2026)

1. **Namespace-qualified type alias lookup** - Type aliases like `size_t` are now correctly found when used inside `namespace std`, even when registered as `std::size_t` in the type table.

2. **Reference declarators in template arguments** - Patterns like `declval<_Tp&>()` now correctly recognize `_Tp` as a template parameter when followed by `&` or `&&`.

3. **Member type aliases as template arguments** - Type aliases defined within a struct/class can now be used as template arguments in subsequent member definitions (e.g., `using outer = wrapper<inner_type>` where `inner_type` is defined earlier in the same struct).

4. **Member struct/class templates** (January 7, 2026 - Morning) - Template struct and class declarations are now supported as class members, including unnamed variadic parameter packs. This fixes `<type_traits>` compilation blocker at line 1838 where `template<typename...> struct _List { };` is used inside a class. Empty member struct templates now parse correctly. **Note**: Member struct templates with function bodies still need work.

5. **Member struct template partial specialization** (January 7, 2026 - Evening Part 1) - Basic support for partial specialization of member struct templates. Patterns like `template<typename T, typename... Rest> struct List<T, Rest...> : List<Rest...> { };` now parse correctly. This advances `<type_traits>` from line 1841 to line 1845. Unique pattern names are generated with modifiers (P=pointer, R=reference, etc.). Test case `test_member_struct_partial_spec_ret0.cpp` successfully compiles!

6. **Non-type value parameters & forward declarations** (January 7, 2026 - Evening Part 2) - Extended partial specialization support to handle non-type value arguments (e.g., `struct Select<N, T, true>` where `true` is a boolean value). Added support for `using` declarations in partial specialization bodies and forward declarations of member struct templates (`template<...> struct Name;`). This **advances `<type_traits>` from line 1845 to line 1917 (72 more lines!)**. Pattern names now include value markers (e.g., `_V1` for true). Test case `test_member_partial_spec_nontype_value_ret0.cpp` successfully compiles!

7. **Template full specialization forward declarations** (January 8, 2026) - Added support for forward declarations of full template specializations for top-level templates. Patterns like `template<> struct make_unsigned<bool>;` now parse correctly without requiring a body definition. The parser detects `;` after template arguments and registers the specialization as a forward declaration. This **advances `<type_traits>` from line 1917 to line 2180 (263 more lines!)**. Test case `test_template_full_spec_forward_decl_ret0.cpp` successfully compiles!

8. **__alignof__ operator** (January 8, 2026 - Morning) - Added support for the GCC/Clang `__alignof__` extension, which is treated as an identifier but behaves like the standard `alignof` operator. Works with both type and expression forms, including with `typename` in template contexts (e.g., `__alignof__(typename T::type)`). This **advances `<type_traits>` from line 2180 to line 2244 (64 more lines!)**. Test case `test_alignof_extension_ret0.cpp` successfully compiles! **Total progress from line 1917 to 2244: 327 lines!**

9. **Static member variable definitions outside class body for template classes** (January 8, 2026 - Afternoon) - Added support for out-of-class definitions of static member variables for template classes without initializers. Patterns like `template<typename T> const size_t ClassName<T>::memberName;` now parse correctly. This is used to provide storage for static constexpr members that were declared (with initializer) inside the class body. The parser now recognizes the pattern and registers it appropriately. This **advances `<type_traits>` from line 2244 to line 2351 (107 more lines!)**. Test case `test_template_static_member_outofline_ret42.cpp` successfully compiles! **Total session progress: 434 lines!** **Next blocker**: decltype expression evaluation at line 2351.

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

## Latest Investigation (January 8, 2026 - Evening - Part 2)

### Anonymous Union Bug Fixed! 🎉

**Fix Implemented:** Anonymous union members are now properly flattened into the parent struct during parsing.

**What Changed:**
- Modified Parser.cpp line 4172-4197 to parse anonymous union members instead of skipping them
- Anonymous union members are now added directly to the parent struct's member list
- This allows proper member lookup in both regular structs and template classes

**Test Results:**
- ✅ `test_anonymous_union_member_access_ret0.cpp` - **NOW PASSES** (was hanging)
- ✅ `test_template_anonymous_union_access_ret0.cpp` - **NOW PASSES** (was "Missing identifier" error)
- ⚠️ `test_union_member_access_fail.cpp` - **Still fails** (named unions with member access cause segfault)

**Examples:**
```cpp
// ✅ This now works!
template<typename T>
struct Container {
    union {
        char dummy;
        T value;
    };
    T& get() { return value; }  // Works now!
};

// ✅ This also works!
struct MyStruct {
    union {
        int i;
        float f;
    };
};
MyStruct s;
s.i = 42;  // Works now!

// ⚠️ Named unions still have issues
struct S {
    union { int i; } data;  // Named union member
};
S s;
s.data.i = 42;  // Still causes segfault
```

**Remaining Issue:**
Named unions (where the union itself has a member name like `union {...} data;`) still cause segfaults during codegen. This is a separate issue from anonymous unions and requires different handling.

## Latest Investigation (January 8, 2026 - Evening - Part 1)

### Critical Bug Found: Union Member Access Causes Compilation Hangs

During investigation of union support, discovered a **critical bug** that causes the compiler to hang indefinitely:

**Bug Details:**
- ❌ **Accessing union members (named or anonymous) in structs causes infinite loop**
- ✅ Declaring unions works fine
- ❌ But any attempt to read or write union member fields causes compilation to hang
- This affects BOTH regular structs and template classes
- This is the root cause blocking `<optional>` and `<variant>` headers

**UPDATE:** This bug has been **partially fixed**! Anonymous unions now work. See section above.

## Latest Investigation (January 8, 2026 - Afternoon)

### Key Findings

1. **`<limits>` header now compiles successfully!** ✅
   - Compilation time: ~1.8 seconds
   - Successfully instantiates `std::numeric_limits<int>` and `std::numeric_limits<float>`
   - Can call `max()` member function
   - Test case: `test_std_limits.cpp` produces valid output

2. **Requires clauses for C++20 concepts work correctly** ✅
   - Basic concept definitions work: `concept Integral = __is_integral(T);`
   - Requires clauses on template functions: `requires Integral<T>`
   - The `<concepts>` header timeout is due to template instantiation volume, not missing requires clause support

3. **Template instantiation volume remains the primary blocker**
   - Most headers that timeout are not due to missing features
   - Individual template instantiations are fast (~20-50μs)
   - Standard headers contain hundreds to thousands of template instantiations
   - This is a performance optimization issue, not a feature gap

4. **Floating-point arithmetic bug fixed** ✅
   - Fixed critical bug where float/double operations returned garbage
   - Bug was in `storeArithmeticResult()` not storing XMM register results to memory
   - All floating-point arithmetic now works correctly

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
