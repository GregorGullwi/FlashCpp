# Standard Header Tests

This directory contains test files for C++ standard library headers to assess FlashCpp's compatibility with the C++ standard library.

## Test Files

### Standard Header Test Files (`test_std_*.cpp`)

These files test FlashCpp's ability to compile and use various C++ standard library headers:

| Header | Test File | Status | Notes |
|--------|-----------|--------|-------|
| `<type_traits>` | `test_std_type_traits.cpp` | ✅ Compiled | Successfully compiles! Function ref types added Jan 12, 2026 |
| `<string_view>` | `test_std_string_view.cpp` | ⏱️ Timeout | Now progresses further after function pointer typedef fix (Jan 13) |
| `<string>` | `test_std_string.cpp` | ⏱️ Timeout | Allocators, exceptions |
| `<iostream>` | `test_std_iostream.cpp` | ⏱️ Timeout | Virtual inheritance, locales |
| `<tuple>` | `test_std_tuple.cpp` | ⏱️ Timeout | Variadic templates, times out during compilation |
| `<vector>` | `test_std_vector.cpp` | ⏱️ Timeout | Now progresses further after operator new/delete fix (Jan 13) |
| `<array>` | `test_std_array.cpp` | ⏱️ Timeout | Times out during compilation |
| `<algorithm>` | `test_std_algorithm.cpp` | ⏱️ Timeout | Now progresses further after operator new/delete fix (Jan 13) |
| `<utility>` | `test_std_utility.cpp` | ❌ Failed | Blocked at `move.h:215` - noexcept with dependent templates |
| `<memory>` | `test_std_memory.cpp` | ⏱️ Timeout | Smart pointers, allocators |
| `<functional>` | `test_std_functional.cpp` | ⏱️ Timeout | std::function, type erasure |
| `<map>` | `test_std_map.cpp` | ⏱️ Timeout | Now progresses further after operator new/delete fix (Jan 13) |
| `<set>` | `test_std_set.cpp` | ⏱️ Timeout | Now progresses further after operator new/delete fix (Jan 13) |
| `<optional>` | `test_std_optional.cpp` | ⏱️ Timeout | Now progresses further after operator new/delete fix (Jan 13) |
| `<variant>` | `test_std_variant.cpp` | ⏱️ Timeout | Now progresses further after operator new/delete fix (Jan 13) |
| `<any>` | `test_std_any.cpp` | ⏱️ Timeout | Type erasure, RTTI |
| `<span>` | `test_std_span.cpp` | ⏱️ Timeout | Now progresses further after operator new/delete fix (Jan 13) |
| `<concepts>` | `test_std_concepts.cpp` | ❌ Failed | Blocked at `concepts:130` - compound requirement in requires |
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
7. **Function pointer typedef** (`typedef void (*handler)()`) - ✅ Implemented (Jan 13, 2026)
8. **Operator new/delete at global scope** - ✅ Implemented (Jan 13, 2026)
9. **Template function = delete/default** - ✅ Implemented (Jan 13, 2026)
10. **Complex noexcept expressions with dependent templates** - ⚠️ Not yet implemented
11. **Namespace-qualified variable templates with partial specializations** - ✅ Implemented (Jan 13, 2026)

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

## Latest Investigation (January 13, 2026 - Namespace-Qualified Variable Templates with Partial Specializations)

### ✅ IMPLEMENTED: Namespace-Qualified Variable Template Support

**Pattern Now Supported:** Variable templates accessed with namespace qualification and partial specializations for reference/pointer types:
```cpp
namespace std {
    template<typename T>
    inline constexpr bool is_reference_v = false;

    template<typename T>
    inline constexpr bool is_reference_v<T&> = true;

    template<typename T>
    inline constexpr bool is_reference_v<T&&> = true;
}

int main() {
    bool is_int_ref = std::is_reference_v<int>;      // false
    bool is_ref = std::is_reference_v<int&>;         // true - uses partial specialization
    bool is_rref = std::is_reference_v<int&&>;       // true - uses partial specialization
    return (!is_int_ref && is_ref && is_rref) ? 0 : 1;
}
```

**What Was Fixed:**
1. ✅ **Namespace-qualified variable template registration** - Variable templates in namespaces are now registered with both simple names (`is_reference_v`) and qualified names (`std::is_reference_v`)
2. ✅ **Variable template lookup in qualified identifier parsing** - When parsing `ns::template<args>`, the parser now checks for variable templates before class templates
3. ✅ **Partial specialization pattern matching** - Variable template partial specializations (e.g., `is_reference_v<T&>`, `is_reference_v<T&&>`) are now correctly matched based on template argument characteristics
4. ✅ **Reference qualifier detection in template arguments** - When parsing template arguments like `<int&>`, the parser now correctly recognizes `&` and `&&` as reference modifiers (previously interpreted as binary operators)
5. ✅ **Pointer modifier detection in template arguments** - Similarly, `*` is now recognized as a pointer modifier when following an identifier

**Implementation:**
- Modified `parse_explicit_template_arguments()` to detect when a simple identifier is followed by `&`, `&&`, or `*` and fall through to type parsing instead of accepting it as an expression
- Added variable template lookup in `parse_primary_expression()` for qualified identifiers with template arguments
- Modified `try_instantiate_variable_template()` to check partial specialization patterns based on `is_reference`, `is_rvalue_reference`, and `pointer_depth` properties
- Fixed symbol table registration to use simple names (without `::`) for proper lookup during code generation

**Test Cases:**
- ✅ All 902 existing tests pass
- Variable template with namespace qualification and partial specializations works correctly

**Impact:**
- ✅ Variable templates like `std::is_reference_v<T>`, `std::is_pointer_v<T>`, etc. now work correctly with namespace qualification
- This is a key building block for `<type_traits>` and other standard library headers

---

## Previous Investigation (January 12, 2026 - Named Anonymous Unions in Typedef & Direct Initialization)

### ✅ IMPLEMENTED: Named Anonymous Unions in Typedef Struct Bodies

**Pattern Now Supported:** Named anonymous unions/structs inside typedef struct bodies, commonly found in system headers like `<wchar.h>` and `<bits/types/__mbstate_t.h>`:
```cpp
typedef struct
{
  int __count;
  union
  {
    unsigned int __wch;
    char __wchb[4];
  } __value;		// Named anonymous union member
} __mbstate_t;
```

**What Was Fixed:**
1. ✅ **Named anonymous union detection in typedef struct parsing** - Added peek-ahead logic to check if there's an identifier after the closing `}` of the union
2. ✅ **Anonymous union type creation** - Creates a unique anonymous type with proper member layout
3. ✅ **Member declaration creation** - Adds the named member to the enclosing typedef struct

**Implementation:**
- Modified `parse_typedef_declaration()` in the inline struct body parsing section
- After seeing `union {` or `struct {`, skips balanced braces to peek if an identifier follows
- If identifier follows, creates an anonymous type, parses members into it, and adds the named member
- If no identifier, treats as true anonymous union (flattened into parent)

**Test Cases:**
- ✅ `test_named_anonymous_union_in_typedef_ret42.cpp` - Named anonymous union inside typedef struct

**Impact:**
- ✅ `<string_view>` header now progresses past `__mbstate_t.h`!
- Previously blocked at `/usr/include/x86_64-linux-gnu/bits/types/__mbstate_t.h:20`

---

### ✅ IMPLEMENTED: Improved Direct Initialization Detection

**Pattern Now Supported:** Direct initialization with dereference expressions like `*this` or `*ptr`:
```cpp
struct fpos {
    fpos copy_self() const {
        fpos __pos(*this);  // Direct initialization with *this
        return __pos;
    }
};
```

**What Was Fixed:**
- ✅ **`*this` and `*variable` in direct initialization** - The `looks_like_function_parameters()` heuristic was incorrectly treating `*` at the start of parentheses as a pointer parameter type
- Modified the function to check what follows the `*` operator:
  - If followed by `this`, it's `*this` = expression (direct init)
  - If followed by a known variable, it's `*var` = expression (direct init)
  - If followed by a literal or open paren, it's an expression
  - Otherwise, assume function parameter

**Test Cases:**
- ✅ `test_direct_init_variable_ret42.cpp` - Direct initialization with named variable

**Impact:**
- ✅ `<string_view>` header now progresses past `fpos` copy patterns!
- Previously blocked at `/usr/include/c++/14/bits/postypes.h:156`

---

### ✅ IMPLEMENTED (Jan 13, 2026): Function Pointer Typedef

**Pattern Now Supported:**
```cpp
typedef void (*new_handler)();
```

This is a function pointer typedef - declaring a type alias for a pointer to a function returning void with no parameters. This pattern is common in `<new>` header.

**Status:** ✅ **FULLY IMPLEMENTED**
- Test case: `tests/test_func_ptr_typedef_ret0.cpp` - Returns 0 ✅
- `<new>` header now parses past line 108!

---

## Previous Investigation (January 12, 2026 - Function Reference/Pointer Types)

### ✅ IMPLEMENTED: Function Reference and Pointer Types in Type Aliases and Template Arguments

**Pattern Now Supported:** Function reference and pointer types used in type aliases and template arguments, enabling `<type_traits>` to compile completely:
```cpp
// Function reference types in type aliases
using func_ref = int (&)();    // lvalue reference to function
using func_rref = int (&&)();  // rvalue reference to function
using func_ptr = int (*)();    // pointer to function

// Function reference types in template arguments (key blocker for <type_traits>)
template<typename T> T declval();
template<typename _Xp, typename _Yp>
using __cond_res = decltype(false ? declval<_Xp(&)()>()() : declval<_Yp(&)()>()());
```

**What Was Fixed:**
1. ✅ **Function reference types in template arguments** - Modified `parse_explicit_template_arguments()` to recognize and parse `T(&)()`, `T(&&)()`, and `T(*)()` patterns
2. ✅ **Function reference types in global type aliases** - Modified `parse_using_directive_or_declaration()` to handle function type syntax after the base type
3. ✅ **Function reference types in member type aliases** - Modified `parse_member_type_alias()` with the same handling
4. ✅ **Rvalue reference (`&&`) token handling in template alias declarations** - Fixed to handle both single `&&` token and two `&` tokens

**Implementation:**
- Added function type detection after base type parsing: checks for `(` followed by `&`, `&&`, or `*`, then `)`, then `(` for parameter list
- Creates `FunctionSignature` with return type and parameter types
- Properly sets reference qualifier for function references
- Added handling in three key locations: template arguments, global type aliases, and member type aliases

**Test Cases:**
- ✅ `test_function_ref_type_ret42.cpp` - Function reference and pointer types in global type aliases
- ✅ `test_std_type_traits.cpp` - Full `<type_traits>` header now compiles successfully!

**Impact:**
- ✅ `<type_traits>` header now **FULLY COMPILES** (~7 seconds)
- The header was previously blocked at line 3834 with `declval<_Xp(&)()>()()` pattern
- This was one of the most important blockers for standard library support

---

## Previous Investigation (January 12, 2026 - Constexpr Function Call Evaluation in Deferred Base Classes)

### ✅ FIXED: Compile-Time Evaluation of Constexpr Function Calls in Deferred Base Class Template Arguments

**Pattern Now Fully Working:** Constexpr function calls in deferred base class template arguments are now evaluated correctly at template instantiation time:
```cpp
// A function that takes a type and returns bool
template<typename Result>
constexpr bool call_is_nt(typename Result::__invoke_type) {
    return true;
}

// This pattern NOW WORKS CORRECTLY!
template<typename Result>
struct test : bool_constant<call_is_nt<Result>(typename Result::__invoke_type{})>
{ };

// test<MyResult>::value is now correctly evaluated to true
```

**What Was Fixed:**
1. ✅ **Constexpr flag preservation in template function instantiation** - Added copying of constexpr/consteval/constinit flags from original template function to instantiated function in `try_instantiate_template_explicit()`
2. ✅ **Constexpr flag capture during template function parsing** - Added `parse_declaration_specifiers()` call in `parse_template_function_declaration_body()` to capture constexpr specifiers before function body parsing
3. ✅ **FunctionCallNode handling in deferred base resolution** - Added handling for `FunctionCallNode` expressions in the deferred base class argument evaluation, with template parameter substitution and constexpr function evaluation
4. ✅ **Simple constexpr function evaluation** - For constexpr functions with a single return statement returning a constant value, the value is now extracted and used for the base class template argument

**Implementation:**
- Modified `try_instantiate_template_explicit()` to copy function specifiers (constexpr, consteval, constinit, noexcept, variadic, linkage, calling convention)
- Modified `parse_template_function_declaration_body()` to call `parse_declaration_specifiers()` and apply flags to the function declaration
- Added `FunctionCallNode` handling in the deferred base class resolution loop to:
  - Extract and substitute template arguments
  - Instantiate the template function
  - Check if the function is constexpr
  - Extract the return value from simple single-statement constexpr functions

**Test Cases:**
- ✅ `test_typename_brace_init_ret0.cpp` - Complex typename brace init as function argument with constexpr evaluation (returns 0)

---

## Previous Investigation (January 12, 2026 - Typename Brace Initialization in Expression Context)

### ✅ IMPLEMENTED: `typename T::type{}` Constructor Calls in Expression Context

**Pattern Now Supported:** The `typename Type::member{}` syntax for creating temporaries of dependent types is now parsed correctly:
```cpp
// A function that takes a type and returns bool
template<typename Result>
constexpr bool call_is_nt(typename Result::__invoke_type) {
    return true;
}

// This pattern NOW PARSES CORRECTLY!
template<typename Result>
struct test : bool_constant<call_is_nt<Result>(typename Result::__invoke_type{})>
{ };
```

**What Was Fixed:**
1. ✅ **`typename` keyword handling in expression context** - The expression parser now recognizes `typename` as the start of a dependent type constructor call
2. ✅ **Qualified dependent type name parsing** - Handles `typename T::type` and `typename T::nested::type` patterns
3. ✅ **Brace initialization `{}` parsing** - Creates a `ConstructorCallNode` for `typename Type{}` patterns
4. ✅ **Parenthesis initialization `()` parsing** - Also supports `typename Type(args)` patterns

**Implementation:**
- Added new handling at the start of `parse_primary_expression()` for the `typename` keyword
- Parses the qualified type name (e.g., `Result::__invoke_type`)
- Parses `{}` or `()` initializers with arguments
- Creates a `ConstructorCallNode` with a `Type::UserDefined` type specifier containing the qualified name

---

## Previous Investigation (January 12, 2026 - Dependent Function Calls as Template Arguments)

### ✅ FIXED: Template Function Calls as Non-Type Template Arguments

**Status Upgrade:** This was previously marked as "PARTIAL FIX" because the parsing worked but the compile-time evaluation didn't. With the constexpr evaluation fixes above, this pattern now works fully.

**Pattern Now Supported:** Template function calls can now be used as non-type template arguments:
```cpp
template<bool B>
struct bool_constant { static constexpr bool value = B; };

template<typename T>
constexpr bool test_func() { return true; }

// This pattern NOW WORKS!
template<typename T>
struct wrapper : bool_constant<test_func<T>()>
{ };
```

**What Was Fixed:**
1. ✅ **FunctionCallNode preservation in expression parsing** - When parsing `func<T>()` as a template argument, expression parsing now creates a `FunctionCallNode` instead of just an `IdentifierNode`
2. ✅ **Dependent function call AST creation** - For template-dependent function calls, the parser now creates a placeholder `FunctionCallNode` with template arguments, preserving the function call structure for later resolution
3. ✅ **Template argument node capture** - Modified `parse_explicit_template_arguments` to capture AST nodes for dependent function calls

**Implementation:**
- Modified `parse_primary_expression()` to capture template argument AST nodes in a new `explicit_template_arg_nodes` vector
- When `has_dependent_template_args` is true, creates a `FunctionCallNode` with a placeholder `DeclarationNode` instead of creating just an `IdentifierNode`
- The `FunctionCallNode` stores the template argument nodes for later resolution during instantiation

**Test Cases:**
- ✅ `test_simple_template_func_call_ret0.cpp` - Simple template function call as template argument

---

## Previous Investigation (January 12, 2026 - Member Function Templates in Partial Specializations)

### ✅ FIXED: bad_any_cast Crash with Member Function Templates in Partial Specializations

**Bug:** FlashCpp crashed with `std::bad_any_cast` when parsing member function templates inside partial specializations:
```cpp
template<typename _Result, typename _Ret>
struct __is_invocable_impl<_Result, _Ret, false, void_t<typename _Result::type>>
{
    template<typename _Tp>
    static void _S_conv(_Tp) noexcept;  // ← CRASHED HERE (before fix)
};
```

**Root Cause:**
- When iterating over member functions to finalize the struct layout, the code assumed all `function_declaration` nodes were `FunctionDeclarationNode`
- But member function templates are stored as `TemplateFunctionDeclarationNode`
- Line 23466 in `parse_template_declaration()` called `.as<FunctionDeclarationNode>()` without checking the actual type first

**What Was Fixed:**
- Modified member function iteration in `parse_template_declaration()` (line 23466) to check for `TemplateFunctionDeclarationNode` before `FunctionDeclarationNode`
- For member function templates, the code now extracts the inner `FunctionDeclarationNode` from the template wrapper

**Implementation:**
```cpp
if (member_func_decl.function_declaration.is<TemplateFunctionDeclarationNode>()) {
    // Extract inner function declaration from template wrapper
    const TemplateFunctionDeclarationNode& template_decl = ...;
    const FunctionDeclarationNode& func_decl = template_decl.function_declaration().as<FunctionDeclarationNode>();
    // ...
} else {
    // Regular member function
    const FunctionDeclarationNode& func_decl = member_func_decl.function_declaration.as<FunctionDeclarationNode>();
    // ...
}
```

**Test Cases:**
- ✅ `tests/test_member_function_template_in_partial_spec_ret0.cpp` - Compiles successfully
- ✅ All 891 existing tests pass

**Impact:**
- ✅ `<type_traits>` no longer crashes during compilation
- ✅ Patterns like `__is_invocable_impl` from libstdc++ now parse correctly
- Note: `<type_traits>` header still exits with code 1 (no error message displayed) - likely hits other unsupported patterns during compilation. Further investigation needed to identify remaining blockers.

---

## Previous Investigation (January 12, 2026 - Member Template Alias Rvalue Reference Declarators)

### ✅ IMPLEMENTED: Member Template Alias Rvalue Reference Declarators in Partial Specializations

**Pattern Now Supported:** Member template aliases with rvalue reference (`&&`) declarators after `typename X::type`, particularly in partial specializations:
```cpp
template<typename _Tp>
struct __xref {
    template<typename _Up> 
    using __type = typename __copy_cv<_Tp>::type;
};

// Partial specialization with && pattern argument and && in alias
template<typename _Tp>
struct __xref<_Tp&&> {
    template<typename _Up> 
    using __type = typename __copy_cv<_Tp>::type&&;  // ← NOW WORKS!
};
```

**What Was Fixed:**
- ✅ **`&&` as single token in member template aliases** - The parser now handles `&&` as a single token (in addition to two separate `&` tokens) when parsing reference declarators in `parse_member_template_alias()`
- This pattern is used extensively in `<type_traits>` around line 3900 for `__xref` and `basic_common_reference` templates

**Why This Matters:**
- The `<type_traits>` header uses this pattern for `__xref` partial specializations
- The `basic_common_reference` template requires this for proper reference handling
- Without this fix, the parser would fail with "Expected ';' after member template alias declaration"

**Implementation:**
- Modified `parse_member_template_alias()` in `src/Parser.cpp` (around line 25017-25033)
- Added check for `&&` as a single token alongside existing `&` handling
- Mirrors the fix already present in `parse_typedef_declaration()` at line 3449

**Test Cases:**
- ✅ `tests/test_member_template_alias_rvalue_ref_ret0.cpp` - Compiles successfully

**Impact:**
- ✅ Unblocks parsing of `__xref` partial specializations in `<type_traits>`
- Note: `<type_traits>` still has `std::bad_any_cast` crash around line 3900 which is a separate issue

---

## Previous Investigation (January 12, 2026 - throw() Exception Specifier Support)

### ✅ IMPLEMENTED: throw() Exception Specifier on Constructors/Destructors

**Pattern Now Supported:** The old-style `throw()` exception specifier (pre-C++17) used in standard library headers like `<new>` and `<exception>`:
```cpp
class bad_alloc {
public:
    bad_alloc() throw() { }                        // ← NOW WORKS!
    ~bad_alloc() throw() { }                       // ← NOW WORKS!
    virtual const char* what() const throw() { }   // ← Already worked (member functions)
};
```

**What Was Fixed:**
1. ✅ **Constructor throw() specifier** - Handled in 3 code paths:
   - Regular struct constructor parsing
   - Template full specialization constructor parsing
   - Template partial specialization constructor parsing
2. ✅ **Destructor throw() specifier** - Already handled via `parse_function_trailing_specifiers()`
3. ✅ **Member function throw() specifier** - Already handled via `parse_function_trailing_specifiers()`

**Why This Matters:**
- The `<new>` header defines `bad_alloc` and `bad_array_new_length` with `throw()` specifiers
- The `<exception>` header also uses this pattern extensively
- Many pre-C++17 standard library implementations use `throw()` instead of `noexcept`
- GCC's libstdc++ still uses `throw()` in many places for compatibility

**Implementation:**
- Modified constructor parsing in `parse_struct_declaration()` to check for `throw` keyword after parameter list
- Added same handling in template full and partial specialization constructor parsing
- Consumes `throw()` and sets constructor as noexcept (semantically equivalent)

**Test Cases:**
- ✅ `tests/test_throw_specifier_ret42.cpp` - Returns 42 ✅

**Impact:**
- ✅ `<new>` header now progresses past `bad_alloc` class parsing
- Note: `<new>` still has other issues (function pointer typedefs: `typedef void (*new_handler)();`)

---

## Previous Investigation (January 11, 2026 - Typedef Reference Declarators and Constructor/Destructor noexcept)

### ✅ IMPLEMENTED: Typedef Reference Declarators

**Pattern Now Supported:** Typedef declarations with reference declarators inside template class bodies:
```cpp
template<class E>
class container {
public:
    typedef E value_type;
    typedef const E& const_reference;   // ← NOW WORKS!
    typedef E& reference;               // ← NOW WORKS!
    typedef E&& rvalue_reference;       // ← NOW WORKS!
    typedef const E* const_pointer;
    typedef E* pointer;
};
```

**What Was Fixed:**
1. ✅ **Lvalue reference declarators (`&`)** - `typedef const T& reference;`
2. ✅ **Rvalue reference declarators (`&&`)** - `typedef T&& rvalue_ref;` (both single `&&` token and two `&` tokens)

**Implementation:**
- Modified `parse_typedef_declaration()` in `src/Parser.cpp` to handle reference declarators after the type specifier
- Added handling for both `&` (lvalue) and `&&` (rvalue) reference tokens

**Test Cases:**
- ✅ `tests/test_typedef_reference_ret0.cpp` - All typedef reference patterns

**Impact:**
- ✅ `<initializer_list>` header now progresses past line 49!
- Previously blocked on `typedef const _E& reference;`

---

### ✅ IMPLEMENTED: Constructor/Destructor noexcept Specifier

**Pattern Now Supported:** Constructor and destructor declarations with `noexcept` specifier:
```cpp
class test_class {
public:
    test_class() noexcept : value_(0) { }
    virtual ~test_class() noexcept { }
};
```

**What Was Fixed:**
1. ✅ **Constructor trailing specifiers** - `noexcept`, `throw()`, attributes parsed after constructor parameters
2. ✅ **Destructor trailing specifiers** - Same handling for destructors

**Implementation:**
- Added call to `parse_function_trailing_specifiers()` after parsing constructor/destructor parameters
- Added `set_noexcept()` and `is_noexcept()` methods to `ConstructorDeclarationNode` and `DestructorDeclarationNode`

**Impact:**
- ✅ `<exception.h>` header now progresses past line 63!
- Previously blocked on `virtual ~exception() _GLIBCXX_TXN_SAFE_DYN _GLIBCXX_NOTHROW;`

---

### ✅ IMPLEMENTED: Explicit Constructor Keyword

**Pattern Now Supported:** `explicit` keyword on constructors:
```cpp
class type_info {
protected:
    explicit type_info(const char* __n) : __name(__n) { }
};
```

**What Was Fixed:**
- Added `explicit` keyword handling in struct member parsing loop alongside `constexpr`, `consteval`, and `inline`

**Impact:**
- ✅ `<typeinfo>` header now progresses past line 151!

---

### Current Test Results (January 11, 2026)

| Status | Count | Headers |
|--------|-------|---------|
| ✅ Compiled | 1 | `<limits>` |
| ⏱️ Timeout | 7 | `<string>`, `<iostream>`, `<vector>`, `<memory>`, `<functional>`, `<ranges>`, `<chrono>` |
| ❌ Failed | 13 | `<type_traits>`, `<string_view>`, `<tuple>`, `<array>`, `<algorithm>`, `<utility>`, `<map>`, `<set>`, `<optional>`, `<variant>`, `<any>`, `<span>`, `<concepts>` |

**Progress Notes:**
- ✅ Typedef reference declarators now work in template class bodies
- ✅ Constructor/destructor noexcept specifiers now work
- ✅ Explicit constructor keyword now handled
- ✅ All 888 existing tests still pass
- ✅ **Operator call syntax fixed!** `operator==(__arg)` call syntax in `<typeinfo>` line 114

---

### ✅ IMPLEMENTED: Operator Call Syntax (`operator==(arg)`)

**Pattern Now Supported:** Calling operators by name using the explicit member function call syntax:
```cpp
bool operator!=(const MyType& other) const {
    return !operator==(other);  // ← NOW WORKS!
}
```

**What Was Fixed:**
1. ✅ **Parser: operator keyword in expression context** - Handles `operator==(args)` syntax within member functions
2. ✅ **CodeGen: `this` pointer type** - Fixed to include pointer level so member function calls pass `this` directly
3. ✅ **IRConverter: `this` registration** - Now registers `this` in `reference_stack_info_` so codegen uses MOV instead of LEA
4. ✅ **CodeGen: Reference argument handling** - Fixed to use 64-bit pointer size for pass-through of reference arguments

**Implementation Details:**
- Modified `parse_primary_expression()` to recognize `operator` keyword and parse operator symbols
- Supports operator() and operator[] as well as standard operator symbols (==, !=, <, etc.)
- Creates `MemberFunctionCallNode` with implicit `this` object
- Fixed underlying bugs in `this` pointer handling that affected member function calls

**Test Cases:**
- ✅ `tests/test_operator_call_syntax_ret0.cpp` - Returns 0 ✅

**Impact:**
- ✅ `<typeinfo>` header now progresses past line 114!
- Previously blocked on `return !operator==(other);` pattern

---

### Updated Test Results (January 11, 2026)

| Status | Count | Headers |
|--------|-------|---------|
| ✅ Compiled | 1 | `<limits>` |
| ⏱️ Timeout | 7 | `<string>`, `<iostream>`, `<vector>`, `<memory>`, `<functional>`, `<ranges>`, `<chrono>` |
| ❌ Failed | 13 | `<type_traits>`, `<string_view>`, `<tuple>`, `<array>`, `<algorithm>`, `<utility>`, `<map>`, `<set>`, `<optional>`, `<variant>`, `<any>`, `<span>`, `<concepts>` |

**Progress Notes:**
- ✅ All 889 existing tests pass (was 888, added new test)
- ✅ Operator call syntax (`operator==(arg)`) now works
- ⚠️ Next blockers: Complex standard library headers still timeout or have parsing issues

---

## Previous Investigation (January 11, 2026 - Member Alias Template Lookup and Template Name Extraction)

### ✅ IMPLEMENTED: Member Alias Template Lookup in Struct Context

**Pattern Now Supported:** Member alias templates can now be used within the same struct where they are defined:
```cpp
struct Container {
    template<typename T, typename U>
    using cond_t = decltype(true ? declval<T>() : declval<U>());
    
    // Use cond_t within the same struct - NOW WORKS!
    template<typename T, typename U>
    static decltype(cond_t<T, U>()) test_func();
};
```

**What Was Fixed:**
1. ✅ **Member alias template lookup using qualified names** - When looking up an identifier like `cond_t`, the parser now also checks for `EnclosingStruct::cond_t` in the template registry
2. ✅ **Two code paths updated** - Both the main expression parsing and the alias-specific expression parsing now check for member alias templates

**Implementation:**
- Modified `parse_primary_expression()` to build qualified names when inside a struct context
- Uses `struct_parsing_context_stack_` to get the enclosing struct name
- Falls back to qualified lookup when direct lookup fails

**Test Cases:**
- ✅ `test_member_alias_template_ret0.cpp` - Member alias template used within same struct

---

### ✅ IMPLEMENTED: Template Name Extraction Fix for Underscore-Containing Names

**Bug Fixed:** Templates with underscores in their names (like `enable_if`) were incorrectly parsed when resolving dependent member types.

**The Problem:** When resolving a type like `enable_if_void_int::type`, the code looked for the first underscore to extract the template name, yielding `enable` instead of `enable_if`.

**What Was Fixed:**
- Modified the template name extraction algorithm to try progressively longer prefixes
- Checks if each prefix is a registered template before using it
- For `enable_if_void_int`, now correctly finds `enable_if` as the template name

**Implementation:**
- Two locations updated in `Parser.cpp` in the `resolve_dependent_member_alias` lambda and related code
- Uses a while loop to try `enable`, then `enable_if`, etc. until a registered template is found

---

### ✅ IMPLEMENTED: Filter Non-Class Templates in try_instantiate_class_template

**Bug Fixed:** Function templates like `declval` were being passed to `try_instantiate_class_template`, causing errors.

**What Was Fixed:**
- Added an early check at the start of `try_instantiate_class_template()`
- Verifies the template is a `TemplateClassDeclarationNode` before proceeding
- Silently skips function templates, preventing spurious error messages

---

### Current Test Results (January 11, 2026)

| Status | Count | Headers |
|--------|-------|---------|
| ✅ Compiled | 1 | `<limits>` |
| ⏱️ Timeout | 7 | `<string>`, `<iostream>`, `<vector>`, `<memory>`, `<functional>`, `<ranges>`, `<chrono>` |
| ❌ Failed | 13 | `<type_traits>`, `<string_view>`, `<tuple>`, `<array>`, `<algorithm>`, `<utility>`, `<map>`, `<set>`, `<optional>`, `<variant>`, `<any>`, `<span>`, `<concepts>` |

**Progress Notes:**
- ✅ Member alias templates within struct context now work
- ✅ Template names with underscores (like `enable_if`) now extracted correctly
- ✅ Function templates no longer cause "not a TemplateClassDeclarationNode" errors
- ⚠️ `<type_traits>` still crashes with `std::bad_any_cast` - requires further investigation
- All 886 existing tests still pass

---

### Remaining Blockers in `<type_traits>`

1. **`std::bad_any_cast` crash** - The header still causes a crash during parsing; root cause TBD
2. **Complex template metaprogramming patterns** - Some deeply nested patterns may still have issues

---

## Previous Investigation (January 11, 2026 - Member Template and Static Member Resolution)

### ✅ IMPLEMENTED: Member Struct Template Resolution in Partial Specialization Patterns

**Pattern Now Supported:** Partial specializations where template argument patterns reference member struct templates:
```cpp
class __make_unsigned_selector_base {
protected:
    template<typename...> struct _List { };

    template<typename _Tp, typename _U>
    struct Outer;

    template<typename _Tp, typename _U>
    struct Outer<_Tp, _List<_U>> {  // ← _List is a member struct template - NOW WORKS!
        using __type = _Tp;
    };
};
```

**What Was Fixed:**
1. ✅ **Member struct template lookup in partial specialization patterns** - When parsing template argument patterns like `_List<_U>` inside a partial specialization, the parser now checks if the identifier is a member struct template of the enclosing class
2. ✅ **Non-type template parameter recognition in partial specialization patterns** - Parameters like `_Sz` in `struct __select<_Sz, _List<_Uint, _UInts...>, true>` are now properly recognized by setting up `current_template_param_names_` before parsing the pattern

**Implementation:**
- Added check in `parse_primary_expression` to look up member struct templates in `gTemplateRegistry` using the qualified name (e.g., `EnclosingClass::MemberTemplate`)
- Modified `parse_member_struct_template` to set up `current_template_param_names_` before calling `parse_explicit_template_arguments()` for the specialization pattern

**Test Cases:**
- ✅ `test_member_template_arg.cpp` - Basic member struct as template argument
- ✅ `test_member_struct_full_pattern.cpp` - Complex pattern with multiple nested member templates

---

### ✅ IMPLEMENTED: Static Data Member Resolution in Template Type Aliases

**Pattern Now Supported:** Type aliases within template structs that reference static data members of the same struct:
```cpp
template<unsigned long _Len>
struct aligned_union {
private:
    static const unsigned long _S_len = _Len > 8 ? _Len : 8;
public:
    static const unsigned long alignment_value = 8;
    using type = typename aligned_storage<_S_len, alignment_value>::type;  // ← NOW WORKS!
};
```

**What Was Fixed:**
- ✅ **Static member lookup during struct body parsing** - Added `local_struct_info` pointer to `StructParsingContext` to track the `StructTypeInfo` being built
- ✅ **Identifier resolution for static members** - When parsing expressions inside a struct body, static members are now looked up in the local `StructTypeInfo`

**Implementation:**
- Extended `StructParsingContext` struct to include `local_struct_info` field
- Updated all three locations where `struct_parsing_context_stack_` is pushed to set the `local_struct_info`
- Added check in `parse_primary_expression` to look up static members from `local_struct_info`

**Test Cases:**
- ✅ `test_static_member_in_type_alias.cpp` - Static members used as template arguments

---

### Current Test Results (January 11, 2026)

| Status | Count | Headers |
|--------|-------|---------|
| ✅ Compiled | 1 | `<limits>` |
| ⏱️ Timeout | 7 | `<string>`, `<iostream>`, `<vector>`, `<memory>`, `<functional>`, `<ranges>`, `<chrono>` |
| ❌ Failed | 13 | `<type_traits>`, `<string_view>`, `<tuple>`, `<array>`, `<algorithm>`, `<utility>`, `<map>`, `<set>`, `<optional>`, `<variant>`, `<any>`, `<span>`, `<concepts>` |

**Progress Notes:**
- `<vector>` moved from Failed to Timeout (progress!)
- ✅ Member alias templates (like `__cond_t`) - **FIXED** (January 11, 2026)
- ✅ Template node type mismatches for `declval` - **FIXED** (January 11, 2026)
- ✅ `enable` vs `enable_if` name extraction - **FIXED** (January 11, 2026)
- Remaining issue: `std::bad_any_cast` crash during type_traits parsing

---

## Previous Investigation (January 10, 2026 - Nested Template Base Classes)

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

### ✅ IMPLEMENTED (Line 2583) - Investigation January 11, 2026

**Previous Blocker (Line 2583):** ✅ **FULLY RESOLVED**

The `::type` member access after alias template resolution is now supported:
```cpp
using type = typename __conditional_t<__or_<is_same<_Argval, _Class>,
    is_base_of<_Class, _Argval>>::value,
    __result_of_memobj_ref<_MemPtr, _Arg>,
    __result_of_memobj_deref<_MemPtr, _Arg>
>::type;  // <-- Line 2583: NOW WORKS!
```

**What Was Fixed:**
- ✅ **Member type access after alias template resolution** - Pattern: `typename alias_template<...>::type`
- After alias template resolution returns a type (e.g., `result_ref<int, Arg>`), check for `::` and parse member access
- Added handling in two code paths: deferred instantiation and non-deferred alias resolution
- Test case: `test_alias_template_member_type_ret42.cpp` - Returns 42 ✅

**Root Cause Analysis:**
- The parser was resolving `conditional_t<...>` to a struct type (e.g., `result_ref<int, Arg>`)
- It then returned immediately without checking if `::type` followed
- This caused "Expected ';' after type alias" error because the caller saw `::`

**Implementation:**
- Modified `parse_type_specifier()` in two locations where alias templates return
- Before returning, check if `peek_token()` is `::`
- If so, consume `::` and the member name, then look up the qualified type
- For dependent types, create a placeholder type

**Progress:** `<type_traits>` now compiles from line 2583 → line 2727 (**144 more lines!**)

---

### ✅ IMPLEMENTED (Line 2727) - Investigation January 11, 2026

**Previous Blocker (Line 2727):** ✅ **FULLY RESOLVED**

The template template parameters with variadic parameters are now supported:
```cpp
template<typename _Def, template<typename...> class _Op, typename... _Args>
    struct __detected_or
    {
      using type = _Def;
      using __is_detected = false_type;
    };
```

**What Was Fixed:**
- ✅ **Template template parameters with variadic packs** - Pattern: `template<typename...> class Op`
- Modified `parse_template_template_parameter_form()` to handle `typename...` inside template template parameters
- Test case: `test_template_template_variadic_ret42.cpp` - Returns 42 ✅

**Implementation:**
- Added ellipsis (`...`) handling after `typename` or `class` keywords in template template parameter parsing
- Sets `is_variadic` flag on the parameter node when `...` is detected

---

### ✅ IMPLEMENTED (Line 2736) - Investigation January 11, 2026

**Previous Blocker (Line 2736):** ✅ **FULLY RESOLVED**

The requires expression type requirements with template arguments are now supported:
```cpp
template<typename _Def, template<typename...> class _Op, typename... _Args>
  requires requires { typename _Op<_Args...>; }
  struct __detected_or<_Def, _Op, _Args...>
```

**What Was Fixed:**
- ✅ **Type requirements with template arguments in requires expressions** - Pattern: `typename Op<Args...>`
- Modified requires expression parsing to handle qualified names and template arguments after `typename`
- The type requirement parser now correctly consumes template argument lists

**Implementation:**
- Enhanced `parse_requires_expression()` to parse full type names including `::` qualifiers and `<...>` template arguments
- Uses balanced angle bracket parsing for template argument lists

---

### Current Status (January 11, 2026)

**Progress:** `<type_traits>` now parses past line 2736 without syntax errors! The header times out during compilation due to template instantiation volume, but no parsing errors are encountered.

**Current Bottleneck:** Template instantiation performance (timeouts) - This is a known issue and is being tracked as a separate optimization task.

---

### ✅ IMPLEMENTED (Line 2946 & 3048) - Investigation January 11, 2026

**Previous Blocker (Line 2946):** ✅ **FULLY RESOLVED**

Partial specializations with namespace-qualified base classes are now supported:
```cpp
template<typename _Tp>
  struct __is_swappable_with_impl<_Tp&, _Tp&>
  : public __swappable_details::__do_is_swappable_impl  // <-- Qualified base class
  {
    using type = decltype(__test<_Tp&>(0));
  };
```

**What Was Fixed:**
1. ✅ **Partial specialization base class parsing** - Now handles qualified names like `ns::Base`
2. ✅ **Struct registration with namespace-qualified names** - Structs inside namespaces are now registered with intermediate names (e.g., `inner::Base` for `ns::inner::Base`)
3. ✅ **Base class lookup in namespaces** - `validate_and_add_base_class` now tries namespace-prefixed lookups

**Implementation:**
- Modified partial specialization parsing to loop through `::` tokens when parsing base class names
- Added namespace-qualified name registration for structs (registers both full path and intermediate paths)
- Added fallback lookup in `validate_and_add_base_class` that tries current namespace prefixes

**Test Cases:**
- ✅ `tests/test_template_template_partial_spec_requires_ret42.cpp` - Returns 42 ✅

### ✅ IMPLEMENTED (Line 3048) - Investigation January 11, 2026

**Previous Blocker (Line 3048):** ✅ **FULLY RESOLVED**

The `noexcept(expr)` expression as a non-type template parameter default value is now supported:
```cpp
template<typename _Tp,
         bool _Nothrow = noexcept(_S_conv<_Tp>(_S_get())),  // noexcept as default value
         typename = decltype(_S_conv<_Tp>(_S_get()))>
```

**What Was Fixed:**
- ✅ **Member template function calls in noexcept expressions** - The `getDeclarationNode` helper lambda now handles `TemplateFunctionDeclarationNode` type
- When a member template function like `_S_conv<Tp>` is called inside a noexcept expression, the code can now properly extract the inner `FunctionDeclarationNode`
- The fix was applied to all 3 definitions of the `getDeclarationNode` lambda in `Parser.cpp`

**Implementation:**
- Modified `getDeclarationNode` lambda at lines 14139, 15028, and 15258 to handle `TemplateFunctionDeclarationNode`
- When encountering a `TemplateFunctionDeclarationNode`, extracts the inner `FunctionDeclarationNode` via `function_declaration().as<FunctionDeclarationNode>().decl_node()`

**Test Cases:**
- ✅ `tests/test_noexcept_template_param_default_ret0.cpp` - Returns 0 ✅

**Current Status:**
- `<type_traits>` header parsing now progresses past line 3048
- However, the header still times out due to template instantiation volume (known performance issue)
- This is a known issue and is being tracked as a separate optimization task

---

### ✅ IMPLEMENTED (Line 2737) - Investigation January 11, 2026

**Previous Blocker (Line 2737):** ✅ **FULLY RESOLVED**

Partial specializations with requires clauses are now supported:
```cpp
template<typename _Def, template<typename...> class _Op, typename... _Args>
  requires requires { typename _Op<_Args...>; }
  struct __detected_or<_Def, _Op, _Args...>
  {
    using type = int;
  };
```

**What Was Fixed:**
- ✅ **Re-detection of class template after requires clause** - The `is_class_template` flag is now re-checked after parsing the requires clause
- Previously, `is_class_template` was set before the requires clause, so it was `false` when `requires` was the next token

**Implementation:**
- Added code after `parse_expression()` for requires clause to re-check if `struct` or `class` keyword follows
- Sets `is_class_template = true` if struct/class keyword is detected after requires clause

**Test Cases:**
- ✅ `tests/test_template_template_partial_spec_requires_ret42.cpp` - Returns 42 ✅

---

**Previous Blocker (Line 2578):** ✅ **FULLY RESOLVED**

The pointer-to-member type alias syntax has been implemented:
```cpp
using _MemPtr = _Res _Class::*;
```

**What Was Fixed:**
- ✅ **Pointer-to-member type syntax in type aliases** - Pattern: `using T = Type Class::*;`
- Added handling in both global scope and struct member type alias parsing
- Test case: `test_ptr_to_member_type_alias_ret42.cpp` - Returns 42 ✅

---

### ✅ IMPLEMENTED (Line 2499): Pointer-to-Member Operators and Pack Expansion

**Previous Blocker (Line 2499):** ✅ **FULLY RESOLVED**

The complex pattern from line 2499 is now fully supported:
```cpp
template<typename _Fp, typename _Tp1, typename... _Args>
  static __result_of_success<decltype(
  (std::declval<_Tp1>().*std::declval<_Fp>())(std::declval<_Args>()...)
  ), __invoke_memfun_ref> _S_test(int);
```

**What Was Fixed:**
1. ✅ **Pointer-to-member operators (`.*` and `->*`)** - Added AST node and postfix parsing
2. ✅ **Pack expansion in parentheses** - Pattern: `(expr...)` in decltype/template contexts  
3. ✅ **Pack expansion in function arguments** - Pattern: `func(expr...)`

**Implementation:**
- Added `PointerToMemberAccessNode` for `obj.*ptr` and `obj->*ptr` expressions
- Added `PackExpansionExprNode` for `expr...` patterns
- Modified parenthesized expression parsing to handle `...` before `)`
- Updated 6 function call parsing locations to wrap pack-expanded arguments

**Test Results:**
- ✅ `test_ptr_to_member_decltype.cpp` - Basic `.*` in decltype
- ✅ `test_static_decltype_ptrmem_arg.cpp` - `.*` in template arguments
- ✅ `test_static_decltype_ptrmem_call_arg.cpp` - Complex `(obj.*ptr)(args...)` pattern
- ✅ `test_pack_expansion_paren.cpp` - Parenthesized pack expansion

**Progress:** `<type_traits>` now compiles from line 2499 → line 2578 (**79 more lines!**)

---

**New Blocker (Line 2578):** `Expected ';' after type alias`

This is a different parsing issue unrelated to pointer-to-member or pack expansion.

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
