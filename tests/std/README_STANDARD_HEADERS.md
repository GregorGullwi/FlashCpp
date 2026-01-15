# Standard Header Tests

This directory contains test files for C++ standard library headers to assess FlashCpp's compatibility with the C++ standard library.

## Current Status

| Header | Test File | Status | Blocker |
|--------|-----------|--------|---------|
| `<limits>` | `test_std_limits.cpp` | ✅ Compiled | ~2s |
| `<type_traits>` | `test_std_type_traits.cpp` | ✅ Compiled | ~5.6s |
| `<compare>` | N/A | ✅ Compiled | ~0.4s |
| `<version>` | N/A | ✅ Compiled | ~0.6s |
| `<source_location>` | N/A | ✅ Compiled | ~0.6s |
| `<numbers>` | N/A | ✅ Compiled | ~6.3s (NEW!) |
| `<concepts>` | `test_std_concepts.cpp` | ⏱️ Timeout | Heavy template instantiation |
| `<utility>` | `test_std_utility.cpp` | ⏱️ Timeout | Heavy template instantiation |
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
| `<bit>` | N/A | ⏱️ Timeout | Includes heavy headers |
| `<atomic>` | N/A | ⏱️ Timeout | Heavy headers |
| `<initializer_list>` | `test_std_initializer_list.cpp` | ❌ Failed | Requires special compiler support |
| `<new>` | N/A | ✅ Compiled | ~0.5s (FIXED 2026-01-15) |
| `<exception>` | N/A | ❌ Failed | Missing `_Hash_bytes` function |
| `<ratio>` | N/A | ❌ Failed | Type alias as base class (see blocker #3) |
| `<csetjmp>` | N/A | ❌ Failed | Preprocessor `sizeof` in macro |
| `<csignal>` | N/A | ❌ Failed | Preprocessor `sizeof` in macro |

**Legend:** ✅ Compiled | ❌ Failed | ⏱️ Timeout (>10s)

### C Library Wrappers (Also Working)

| Header | Test File | Notes |
|--------|-----------|-------|
| `<cstddef>` | `test_cstddef.cpp` | `size_t`, `ptrdiff_t`, `nullptr_t` (~0.7s) |
| `<cstdlib>` | `test_cstdlib.cpp` | `malloc`, `free`, etc. |
| `<cstdio>` | `test_cstdio_puts.cpp` | `printf`, `puts`, etc. |
| `<cstdint>` | N/A | `int32_t`, `uint64_t`, etc. (~0.2s) |
| `<cstring>` | N/A | `memcpy`, `strlen`, etc. (~0.8s) (NEW!) |
| `<ctime>` | N/A | `time_t`, `clock`, etc. (~0.6s) (NEW!) |
| `<climits>` | N/A | `INT_MAX`, `LONG_MAX`, etc. (~0.2s) (NEW!) |
| `<cfloat>` | N/A | `FLT_MAX`, `DBL_MIN`, etc. (~0.2s) (NEW!) |
| `<cassert>` | N/A | `assert` macro (~0.2s) (NEW!) |
| `<cerrno>` | N/A | `errno` (~0.2s) (NEW!) |
| `<clocale>` | N/A | `setlocale`, `localeconv` (~0.2s) (NEW!) |

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

### 2. Variable Templates in Type Context (FIXED - 2026-01-14)

**Issue:** ~~Variable templates used as non-type arguments in class template contexts were causing "No primary template found" errors.~~ **RESOLVED**

**Example pattern:**
```cpp
// Variable template like std::is_reference_v
template<typename _Tp>
inline constexpr bool is_reference_v = false;

// Used as non-type argument in class template:
template<typename _Xp, typename _Yp>
struct common_ref_impl : enable_if<is_reference_v<condres_cvref<_Xp>>, condres_cvref<_Xp>> {};
```

**Previous error:** `No primary template found for 'is_reference_v'`

**Root cause:** When parsing `is_reference_v<T>` in a type context (as an argument to `enable_if`), the parser was calling `try_instantiate_class_template()` without first checking if the identifier was a variable template. Variable templates are expressions, not types.

**Fix applied:** Added a check in `parse_type_specifier()` (Parser.cpp) to skip class template instantiation if the identifier is a variable template. The check uses `gTemplateRegistry.lookupVariableTemplate()` with both unqualified and namespace-qualified lookups.

**Test case:** `tests/test_variable_template_in_enable_if_ret0.cpp`

**Previous blockers resolved (January 15, 2026):**
- noexcept(expr) as template argument: Pattern `bool_constant<noexcept(declval<T&>().~T())>` now works
  - **Fix:** Added handling in `parse_explicit_template_arguments()` to accept NoexceptExprNode, SizeofExprNode, AlignofExprNode, and TypeTraitExprNode as dependent template arguments when constant evaluation fails
  - **Test case:** `tests/test_noexcept_template_arg_ret0.cpp`
- Static const member visibility in static_assert: Pattern `static_assert(value == 42, "msg");` within struct now works
  - **Fix:** Added early lookup for static members in `parse_expression()` and passed struct context to ConstExprEvaluator
  - **Test case:** `tests/test_static_assert_member_visibility_ret0.cpp`
- String literal concatenation in static_assert: Multi-line string messages now work
  - **Fix:** Modified `parse_static_assert()` to consume multiple adjacent string literals
  - **Test case:** `tests/test_static_assert_string_concat_ret0.cpp`
- Added `__INTMAX_MAX__`, `__INTMAX_MIN__`, `__UINTMAX_MAX__` predefined macros required by `<ratio>` header

**Previous blockers resolved (January 14, 2026):**
- Variable templates in type context: Pattern `enable_if<is_reference_v<T>, U>` where `is_reference_v` is a variable template
  - **Fix:** Added variable template check in `parse_type_specifier()` before calling `try_instantiate_class_template()`
  - **Test case:** `tests/test_variable_template_in_enable_if_ret0.cpp`
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

### 3. Template Argument Reference Preservation (FIXED - 2026-01-14)

**Issue:** ~~Template argument substitution can lose reference qualifiers when substituting type parameters.~~ **RESOLVED**

**Example pattern:**
```cpp
template<typename T>
constexpr bool test() {
    return is_reference_v<T>;  // T=int& now correctly preserves reference
}

test<int&>();  // Now correctly returns true
test<int&&>(); // Now correctly returns true
```

**Fix applied:** Updated three code paths in `Parser.cpp` and `TemplateRegistry.h`:
1. `toTemplateArgument()`: Check `is_rvalue_reference` BEFORE `is_reference` since both flags are true for rvalue references
2. `try_instantiate_template_explicit()`: Use `toTemplateArgument()` to preserve full type info including references instead of just `makeType(base_type)`
3. `mangleTemplateName()`: Include reference qualifiers (`R` for `&`, `RR` for `&&`) in mangled names to generate distinct instantiations
4. Template parameter registration: Preserve `is_reference_` and `is_rvalue_reference_` in `TypeInfo` when setting up type aliases during template body re-parsing

**Test case:** `tests/test_template_ref_preservation_ret0.cpp`

### 4. Type Alias as Base Class (FIXED - 2026-01-15)

**Issue:** ~~Type aliases (using declarations) cannot currently be used as base classes when qualified with a namespace.~~ **RESOLVED**

**Example that now works:**
```cpp
namespace std {
    template<typename T, T v>
    struct integral_constant { static constexpr T value = v; };
    
    using false_type = integral_constant<bool, false>;
}

struct Test : std::false_type {};  // Now works!
```

**Fix applied:** Two changes in `Parser.cpp`:
1. Type aliases in namespaces now register with namespace-qualified names in `gTypesByName`
2. `validate_and_add_base_class()` now resolves type aliases by following `type_index_` chain to find the underlying struct type

**Test case:** `tests/test_type_alias_base_class_ret0.cpp`

### 5. Template Instantiation Performance

Most headers timeout due to template instantiation volume, not parsing errors. Individual instantiations are fast (20-50μs), but standard headers trigger thousands of instantiations.

**Optimization opportunities:**
- Improve template cache hit rate (currently ~26%)
- Optimize string operations in template name generation
- Consider lazy evaluation strategies

### 5. std::initializer_list Compiler Magic (Known Limitation)

**Issue:** `std::initializer_list<T>` requires special compiler support that is not yet implemented.

**Example that fails:**
```cpp
#include <initializer_list>
int main() {
    std::initializer_list<int> list = {1, 2, 3};  // Error: Too many initializers
    return 0;
}
```

**Expected behavior in standard C++:**
1. The compiler creates a temporary array `int __temp[] = {1, 2, 3}` on the stack
2. The compiler constructs `std::initializer_list` using its private constructor with a pointer to the array and size 3
3. This is compiler magic - the private constructor is only accessible to the compiler

**Current behavior in FlashCpp:**
- FlashCpp treats `std::initializer_list` like any other struct and tries aggregate initialization
- Since `std::initializer_list` only has 2 members (`_M_array` pointer and `_M_len` size), `{1, 2, 3}` fails with "too many initializers"

**Workaround:** Use the default constructor and don't rely on brace-enclosed initializer lists for `std::initializer_list`:
```cpp
std::initializer_list<int> empty_list;  // Works - default constructor
std::initializer_list<int> two = {ptr, 2};  // Works - matches member count
```

**Note:** This is a fundamental limitation that affects many standard library patterns like range-based for loops with initializer lists (`for (int x : {1, 2, 3})`).

### 6. Missing Infrastructure

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
- Function pointer parameters with pack expansion (`void (*)(Args...)`) (NEW)
- Function pointer parameters with noexcept specifier (`void (*)() noexcept`) (NEW)
- Unnamed function pointer parameters (`void (*)()`) (NEW)

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
- Brace initialization for instantiated template structs (NEW)

**C++17/C++20 Features:**
- C++17 nested namespace declarations (`namespace A::B::C { }`)
- C++20 inline nested namespace declarations (`namespace A::inline B { }`)
- Compound requirement noexcept specifier
- Template parameter brace initialization
- Globally qualified `::new`/`::delete`
- Template alias declarations with requires clauses (`template<typename T> requires Constraint<T> using Alias = T;`)
- Template argument reference preservation in function template instantiation

**Other:**
- Named anonymous unions in typedef structs
- Direct initialization with `*this`
- Global scope `operator new`/`operator delete`
- Typedef array syntax (`typedef type name[size];`)
- Function pointer parameters with pack expansion and noexcept (NEW)

## Recent Changes

### 2026-01-15: Ternary Operators in Template Arguments - Fully Working

**Fixed:** Ternary operators (`?:`) inside template arguments are now properly parsed AND evaluated at compile time for runtime use.

**Patterns that now work:**
```cpp
template<intmax_t _Pn>
struct __static_sign
    : integral_constant<intmax_t, (_Pn < 0) ? -1 : 1>
{ };

// Type aliases with ternary template arguments
using positive = holder<(5 < 0) ? -1 : 1>;   // value = 1
using negative = holder<(-5 < 0) ? -1 : 1>;  // value = -1
```

**Fixes applied:**
1. **Parsing fix:** Modified `parse_expression()` to pass `ExpressionContext` to recursive calls for ternary branch parsing, ensuring `>` is recognized as template terminator.

2. **Evaluation fix:** Extended constant expression evaluation in `parse_explicit_template_arguments()` to:
   - Evaluate ternary and binary operator expressions at global scope (not just in SFINAE context)
   - Added `TernaryOperatorNode` and `BinaryOperatorNode` handling to `try_evaluate_constant_expression()`
   - Used `ConstExpr::Evaluator` for complex expression evaluation

3. **API safety:** Removed default `ExpressionContext` argument from `parse_expression()`, `parse_primary_expression()`, `parse_postfix_expression()`, and `parse_unary_expression()` to prevent future context-loss bugs.

**Test case:** `tests/test_ternary_in_template_arg_ret0.cpp` - now verifies runtime values are correct

**Impact:**
- The `<ratio>` header now parses past line 61 (previously failed with ternary parsing error)
- `<ratio>` now fails at line 189 with a different issue: static const members not visible in static_assert within the same struct

**Files Modified:**
- `src/Parser.cpp` - Pass context to ternary branch expression parsing, add ternary/binary evaluation
- `src/Parser.h` - Remove default context arguments

### 2026-01-15: Function Pointer Parameters with Pack Expansion and noexcept

**Fixed:** Function pointer type declarations now properly handle pack expansion, noexcept specifiers, and pointer parameters.

**Patterns that now work:**
- `void (*)(Args...)` - Unnamed function pointer with pack expansion
- `void (*callback)(void*)` - Named function pointer with pointer parameters
- `void (*)() noexcept` - Function pointer with noexcept specifier
- `void (*)() noexcept(expr)` - Function pointer with noexcept expression
- `Args......` - Pack expansion followed by C-style variadic (6 dots)

**Example:**
```cpp
// From <new> header - now compiles!
template<typename _Ret, typename... _Args, bool _NE>
void launder(_Ret (*)(_Args...) noexcept(_NE)) = delete;

// Named function pointer parameter - now works!
void test(void (*callback)(void*)) { }
```

**Technical details:**
- Modified `parse_declarator()` to handle unnamed function pointers (when `*` is followed by `)`)
- Extended `parse_postfix_declarator()` to:
  - Handle pointer levels after parsing parameter types (`void*`, `const int*`, etc.)
  - Consume pack expansion `...` after template parameter types
  - Handle double pack expansion `......` (pack + C variadic)
  - Parse noexcept specifier and noexcept(expr) on function pointer types

**Impact:**
- Unblocks `<new>` header - now compiles successfully (~0.5s)
- Unblocks parsing of `<exception>` header (now fails at function lookup stage, not parsing)
- Enables standard library patterns using complex function pointer types

**Test case:** `tests/test_funcptr_param_pack_ret0.cpp`

**Files Modified:**
- `src/Parser.cpp` - Extended function pointer parsing in `parse_declarator()` and `parse_postfix_declarator()`

### 2026-01-15: Typedef Array Syntax Support

**Fixed:** Typedef declarations with array syntax are now properly parsed.

- Pattern: `typedef long int __jmp_buf[8];` - creates a type alias for an array type
- Previously: Parser expected `;` immediately after the type alias name, failing on `[`
- Now: Parser checks for `[` after the alias name and parses array dimensions
- **Test case:** `tests/test_typedef_array_ret0.cpp`

**Example:**
```cpp
// System header patterns that now work
typedef long int __jmp_buf[8];
typedef char Buffer[256];

int main() {
    __jmp_buf jb;
    jb[0] = 42;
    return 0;
}
```

**Technical details:**
- Added array dimension parsing in `parse_typedef_declaration()` after consuming the alias name
- Uses `ConstExpr::Evaluator::evaluate()` to resolve constant expressions for array sizes
- Updates `TypeSpecifierNode` with `add_array_dimension()` for each dimension
- Supports multidimensional arrays (`typedef int Matrix[3][3];`)

**Impact:**
- Unblocks system headers that use typedef array patterns (e.g., `__jmp_buf` in setjmp.h)
- Enables more C library wrappers to compile
- Note: Some headers like `<csetjmp>` still fail due to preprocessor issues with `sizeof` in macros

**Files Modified:**
- `src/Parser.cpp` - Added typedef array parsing logic
- `tests/test_typedef_array_ret0.cpp` - New test case for typedef array syntax

### 2026-01-14: Brace Initialization for Instantiated Template Structs

**Fixed:** Brace initialization now works correctly for instantiated template structs stored as `Type::UserDefined`.

- Pattern: `TemplateStruct<int> x = {1, 2};` where the struct has 2 members
- Previously: Parser treated `Type::UserDefined` as a scalar type and tried single-value initialization, failing on the comma
- Now: Parser checks if `Type::UserDefined` has `struct_info_` and handles it as a struct-like type
- **Test case:** `tests/test_template_brace_init_userdefined_ret3.cpp`

**Example:**
```cpp
namespace ns {
    template<typename T>
    struct Pair {
        T first;
        T second;
    };
}

int main() {
    ns::Pair<int> p = {1, 2};  // Now works correctly
    return p.first + p.second; // Returns 3
}
```

**Technical details:**
- Modified `parse_brace_initializer()` in `Parser.cpp` (line ~12444)
- Added check: if `type_specifier.type() == Type::UserDefined`, verify if it's a struct-like type by checking `gTypeInfo[type_index].struct_info_`
- If it has struct info, treat it as a struct for aggregate initialization purposes

**Note:** This fix does NOT enable `std::initializer_list` brace initialization (see Known Limitation section above), as that requires special compiler magic.

### 2026-01-14: Variable Templates in Type Context

**Fixed:** Variable templates used as non-type template arguments in class template contexts no longer cause "No primary template found" errors.

- Pattern: `enable_if<is_reference_v<T>, U>` where `is_reference_v` is a variable template, not a class template
- Previously: Parser tried to instantiate `is_reference_v` as a class template, failing with "No primary template found"
- Now: Parser checks if an identifier is a variable template before calling `try_instantiate_class_template()`
- **Test case:** `tests/test_variable_template_in_enable_if_ret0.cpp`

**Technical details:**
1. Added check in `parse_type_specifier()` (Parser.cpp, in the template argument handling section) to skip class template instantiation for variable templates
2. Uses `gTemplateRegistry.lookupVariableTemplate()` with both unqualified and namespace-qualified lookups
3. Variable templates are expressions, not types - they should not trigger class template instantiation

**Progress:** The `<type_traits>` header now compiles in ~8 seconds (was previously failing with the above error).

### 2026-01-14: Template Argument Reference Preservation

**Fixed:** Reference qualifiers (`&` and `&&`) are now properly preserved when instantiating function templates with reference type arguments.

- Pattern: `template<typename T> bool test() { return is_reference_v<T>; }` called with `test<int&>()` or `test<int&&>()`
- Previously: Reference qualifiers were lost during template instantiation, causing `is_reference_v<T>` to always return `false` for reference types passed through a function template
- Now: Each combination of base type and reference qualifier generates a distinct template instantiation (e.g., `test_int`, `test_intR`, `test_intRR`)
- **Test case:** `tests/test_template_ref_preservation_ret0.cpp`

**Technical details:**
1. Fixed `toTemplateArgument()` in `TemplateRegistry.h` to check `is_rvalue_reference` BEFORE `is_reference` (both flags are true for rvalue references)
2. Updated `try_instantiate_template_explicit()` to use `toTemplateArgument()` instead of `makeType(base_type)` to preserve full type info
3. Extended `mangleTemplateName()` to include `R` suffix for lvalue references and `RR` suffix for rvalue references
4. Set `is_reference_` and `is_rvalue_reference_` in `TypeInfo` during template parameter registration

**Progress:** Type traits using variable template partial specializations (like `is_reference_v<T&>`) now work correctly when accessed from within function templates.

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
