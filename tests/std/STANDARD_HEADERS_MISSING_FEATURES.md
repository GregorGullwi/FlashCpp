# Standard Headers Missing Features

This document lists the missing features in FlashCpp that prevent successful compilation of standard C++ library headers. The analysis is based on testing 21 common standard headers.

## Test Results Summary

**UPDATE (January 2, 2026 - Template Brace Initialization)**: 
- ✅ **Template brace initialization (`type_identity<T>{}`)** - Templates can now be instantiated with brace initialization syntax
- ✅ **Dependent template brace initialization** - Patterns like `__type_identity<_Tp>{}` inside templates now work (deferred resolution)
- 🎯 **`<type_traits>` header progressing further** - Previous `__type_identity` errors now fixed
- 🎯 **All 803 tests passing!**

**UPDATE (January 2, 2026 - Earlier)**: Key features for `<type_traits>` implemented!
- ✅ **`__has_builtin()` preprocessor support** - Standard library headers can now detect which compiler intrinsics FlashCpp supports, enabling efficient builtin implementations instead of template fallbacks
- ✅ **Postfix cv-qualifier support** - Template specializations like `struct is_const<T const>` now parse correctly (standard library style)
- ✅ **#ifdef __has_builtin** - Special handling for standard library compatibility
- ✅ **60+ type trait intrinsics** exposed via `__has_builtin()` evaluation

**UPDATE (January 2, 2026)**: Major improvements to template argument parsing!
- ✅ **Type alias recognition in template arguments** - Type aliases that resolve to concrete types (like `IntType = has_value<int>`) are now correctly identified and processed as types rather than dependent expressions
- ✅ **Inherited type alias lookup** - Patterns like `wrapper<T>::type` where `type` comes from a base class now work (via `lookup_inherited_type_alias()` function with StringHandle optimization)
- ✅ **Pack expansion tests fixed** - `test_pack_expansion_template_args_ret42.cpp` now compiles, links and runs correctly
- 🎯 **All 800+ tests passing!****

**UPDATE (January 1, 2026)**: Template argument type alias resolution improved!
- ✅ **Type alias resolution in template argument contexts** - Type aliases like `false_type`, `true_type` can now be used as template arguments (e.g., `first_t<false_type, ...>`)
- ✅ **Template parameter substitution for base classes** - Patterns like `template<typename T1, typename T2> struct __or_<T1, T2> : T1 {}` now work correctly
- ✅ **Improved dependent template argument detection** - Base classes with nested template arguments (e.g., `__or_<is_integral<T>, is_floating_point<T>>`) are now properly deferred

**UPDATE (January 1, 2025)**: New features implemented!
- ✅ **Functional value initialization** `Type()` syntax now works for all builtin types (e.g., `char()`, `int()`, `float()`, `char16_t()`)
- ✅ **All C++ char types** (`char8_t`, `char16_t`, `char32_t`, `wchar_t`) supported in functional cast/init syntax
- ✅ **Floating-point limit macros** added (`__FLT_*`, `__DBL_*`, `__LDBL_*`) for `<limits>` header support
- 🎯 **`<limits>` header now compiles successfully!** (static member data access works; function calls need more work)

**UPDATE (December 28, 2024)**: Inline namespaces now inject their members into the enclosing namespace scope (e.g., `namespace std { inline namespace __1 { ... } }` works without qualifying `__1`).

**UPDATE (December 27, 2024 - Evening)**: Critical parsing fixes implemented!
- ✅ **constexpr typename** in return types now works (e.g., `constexpr typename my_or<...>::type func()`)
- ✅ **sizeof in template parameter defaults** now works (e.g., `template<typename T, size_t N = sizeof(T)>`)
- 🎯 These fixes unblock `<type_traits>` lines 295-306 which were major parsing blockers

**UPDATE (December 27, 2024 - Morning)**: Additional parsing fix for base class member type access implemented. Continue making progress toward `<type_traits>` compilation.

### Successfully Compiling Headers ✅

**C Library Wrappers:**
- `<cstddef>` - ~790ms (provides `size_t`, `ptrdiff_t`, `nullptr_t`) ✅
- `<cstdint>` - ~200ms (provides `int32_t`, `uint64_t`, etc.) ✅  
- `<cstdio>` - ~770ms (provides `printf`, `scanf`, etc.) ✅

**C++ Standard Library:**
- **`<limits>`** - ~1.7s (compiles, static data members work) ✅ **NEW!**

**Combined Test (December 27, 2024):**
- `<cstddef>` + `<cstdint>` together: ~933ms ✅

**C++ Standard Library:**
- **`<type_traits>`** - Partial support, core patterns work, but full header still has parsing issues

### Original Test Results (Before Recent Fixes)
- **Total headers tested**: 21
- **Successfully compiled**: 0 → **NOW: 3+ headers confirmed working!**
- **Timed out (>10s)**: 16
- **Failed with errors**: 5

---

## What Works Today

### ✅ Working Features for Custom Code

While full standard library headers don't compile yet, FlashCpp supports many C++20 features for custom code:

**Type Traits & Intrinsics:**
- All type trait intrinsics (`__is_same`, `__is_class`, `__is_pod`, etc.) ✅
- Custom `integral_constant`-like patterns work ✅
- Conversion operators in all contexts ✅

**Templates:**
- Class templates, function templates, variable templates ✅
- Template specialization (full and partial) ✅
- Variadic templates and fold expressions ✅
- **Pack expansion in decltype base classes** ✅ **NEW!**
- **Qualified base class names** (`ns::Template<Args>::type`) ✅ **NEW!**
- Concepts (basic support) ✅
- CTAD (Class Template Argument Deduction) ✅

**Modern C++ Features:**
- Lambdas (including captures, generic lambdas) ✅
- Structured bindings ✅ **IMPLEMENTED** (December 27, 2024 - Full C++17 support with reference qualifiers)
- Range-based for loops ✅
- `if constexpr` ✅
- constexpr variables and simple functions ✅

**Example - Custom Type Trait:**
```cpp
template<typename T, T v>
struct my_integral_constant {
    static constexpr T value = v;
    constexpr operator T() const { return value; }
};

// Works with implicit conversions
int main() {
    my_integral_constant<int, 42> answer;
    int x = answer;  // ✅ Calls conversion operator
    return x;
}
```

### ❌ What Doesn't Work Yet

**Auto Type Deduction Status (Verified December 27, 2024):**
- ✅ Basic auto works: `auto x = 42;`, `auto y = expr;`
- ✅ Auto with function returns works: `auto p = makePoint();`
- ✅ Auto& references work: `auto& ref = x;`
- ✅ Const auto works: `const auto c = 50;`
- ✅ Auto* pointers work: `auto* ptr = &x;`
- ✅ Auto structured bindings work: `auto [x, y] = pair;` ✅ **NEW!**

**Test**: `test_auto_comprehensive_ret282.cpp` verifies all working auto features ✅

**Remaining Standard Library Blockers:**
- **Full standard library headers still timeout** due to complex template instantiation volume
- **Namespace-qualified template lookup in some contexts** (e.g., `__detail::__or_fn` not found) – **Improved:** inline namespaces now inject into the parent namespace, reducing `std::__1` lookup errors
- Most blockers for core patterns have been resolved, remaining issues are scale and optimization

**Other Headers Still Have Issues:**
- `<vector>`, `<string>`, `<algorithm>` - Not yet tested after recent fixes
- Main remaining concerns: template instantiation performance at scale

**Workaround:** Use C library wrappers (`<cstddef>`, `<cstdint>`, `<cstdio>`) and custom implementations of needed standard library components.

---

## Recent Progress (December 2024)

### ✅ Completed Features

#### 0a. Member Type Access After Template Arguments in Base Classes (December 27, 2024)
**Status**: ✅ **NEWLY IMPLEMENTED**

**What Was Missing**: FlashCpp could not parse member type access (e.g., `::type`) after template arguments when the base class was specified as a simple identifier (not qualified with namespaces).

**The Problem**: Patterns like `struct negation : __not_<T>::type { };` would fail to parse because:
- The parser handled `ns::Template<Args>::type` correctly (qualified case)
- But failed on `Template<Args>::type` (simple identifier case)
- After parsing `Template<Args>`, it never checked for `::type`

**Implementation**: 
- Modified `src/Parser.cpp` lines 3335-3369
- Added check for `::` and member name after parsing template arguments
- Mirrors the logic already present for qualified identifiers (lines 3286-3304)
- Builds fully qualified member type name (e.g., `__not_<T>::type`)
- Properly defers resolution when template arguments are dependent

**Test Cases**:
```cpp
// Now parses successfully:
template<typename T>
struct wrapper {
    using type = int;
};

template<typename T>
struct negation : wrapper<T>::type { };  // ✅ Works!

// From <type_traits>:
template<typename _Pp>
struct negation : __not_<_Pp>::type { };  // ✅ Now parses!
```

**Impact**: Allows more `<type_traits>` patterns to parse correctly. This complements the December 26 work on qualified base class names.

**Files Modified:**
- `src/Parser.cpp` - Base class template argument parsing
- `tests/test_base_class_member_type_access_ret42.cpp` - Test case added

#### 0b. constexpr typename in Return Types (December 27, 2024 - Evening)
**Status**: ✅ **NEWLY IMPLEMENTED**

**What Was Missing**: FlashCpp could not parse `constexpr typename` when both keywords appeared together in function return types. This pattern is common in standard library metaprogramming.

**The Problem**: In `parse_type_specifier()`, the typename check occurred before the function specifier loop:
- Parser saw `constexpr`, entered loop to skip function specifiers
- After consuming `constexpr`, saw `typename` but was past the typename check
- Resulted in error: "Unexpected token in type specifier: 'typename'"

**Example from `<type_traits>` (lines 299-306)**:
```cpp
template <typename _TypeIdentity,
    typename _NestedType = typename _TypeIdentity::type>
  constexpr typename __or_<
    is_reference<_NestedType>,
    is_function<_NestedType>,
    is_void<_NestedType>,
    __is_array_unknown_bounds<_NestedType>
  >::type __is_complete_or_unbounded(_TypeIdentity)
  { return {}; }
```

**Implementation**:
- Modified `src/Parser.cpp` lines 6842-6885
- Moved typename keyword check to AFTER the function specifier loop
- Now handles both `typename` alone and `constexpr typename` patterns
- Added comprehensive comment explaining the ordering requirement

**Test Cases**:
```cpp
// Now parses successfully:
template<bool... Bs>
struct my_or {
    using type = int;
};

template<typename T1, typename T2>
    constexpr typename my_or<true, false>::type
    test_func(T1 a, T2 b) {  // ✅ Works!
    return 42;
}
```

**Impact**: Unblocks multi-line template function declarations in `<type_traits>` where return types span multiple lines with `constexpr typename` patterns.

**Files Modified:**
- `src/Parser.cpp` - Reordered typename check in parse_type_specifier()
- `tests/test_constexpr_multiline_ret42.cpp` - Test case (returns 42) ✅
- `tests/test_multiline_template_function_ret42.cpp` - Additional test

#### 0c. sizeof in Template Parameter Defaults (December 27, 2024 - Evening)
**Status**: ✅ **NEWLY IMPLEMENTED**

**What Was Missing**: FlashCpp could not parse expressions like `sizeof(T)` in non-type template parameter defaults. The parser consumed too many tokens, including the `>` that terminates the template parameter list.

**The Problem**: When parsing default values for non-type template parameters:
- `parse_expression()` was called without context about being inside `< >`
- Expression parser would consume `>` as part of a comparison operator
- Led to error: "Expected expression after '=' in template parameter default"

**Example from `<type_traits>` (line 295)**:
```cpp
template <typename _Tp, size_t = sizeof(_Tp)>
  constexpr true_type __is_complete_or_unbounded(__type_identity<_Tp>)
  { return {}; }
```

**Implementation**:
- Modified `src/Parser.cpp` line 20568
- Changed `parse_expression()` call to include `ExpressionContext::TemplateArgument`
- This context tells the parser to stop at `>` and `,` delimiters
- Prevents over-consumption of tokens

**Test Cases**:
```cpp
// Now parses successfully:
template<typename T, int N = sizeof(T)>
struct test {
    int value;
};

int main() {
    test<int> t;  // N = sizeof(int) = 4
    t.value = 4;
    return t.value;  // ✅ Returns 4
}
```

**Impact**: Enables standard library pattern of using sizeof in template parameter defaults, common in metaprogramming and type computations.

**Files Modified:**
- `src/Parser.cpp` - Added TemplateArgument context to parse_expression call
- `tests/test_sizeof_default_simple_ret4.cpp` - Test case (returns 4) ✅
- `tests/test_sizeof_template_param_default_ret4.cpp` - Additional test case


#### 0d. Type Alias Resolution in Expression Contexts (December 27, 2024 - Late Evening)
**Status**: ✅ **NEWLY IMPLEMENTED**

**What Was Missing**: FlashCpp could not resolve type aliases like `false_type`, `true_type`, `__enable_if_t` when used in expression contexts. The parser only checked `gSymbolTable` for identifiers, but type aliases are registered in `gTypesByName`.

**The Problem**: Standard library headers extensively use type aliases in template metaprogramming:
```cpp
using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

template<typename T>
struct is_const : false_type { };  // false_type used in expression
```

When parsing `false_type` in expression contexts (template arguments, base class specifications), the lookup failed:
- Line 13706-13709: Identifier lookup only checked `gSymbolTable`
- Type aliases registered in `gTypesByName` (line 6315) were not checked
- Led to errors: "Missing identifier: false_type", "Missing identifier: true_type"

**Implementation**:
- Modified `src/Parser.cpp` lines 13713-13725
- Added fallback check to `gTypesByName` when identifier not found in `gSymbolTable`
- Sets `found_as_type_alias` flag to prevent "Missing identifier" errors
- Updated error checks at lines 14771 and 14776 to consider `found_as_type_alias`

**Test Cases**:
```cpp
// Now compiles without errors:
using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

template<typename T>
struct is_const : false_type { };  // ✅ Works!

bool b = true_type::value;  // ✅ Works!
```

**Impact**: Eliminates "Missing identifier" errors for type aliases used in `<type_traits>`, `<optional>`, `<variant>`, and other metaprogramming-heavy headers. This was identified as "Secondary Issue" and "Next Blocker" in the documentation.

**Files Modified:**
- `src/Parser.cpp` - Added gTypesByName fallback in parse_primary_expression()
- Test cases:
  - `tests/test_type_alias_simple_ret42.cpp` - Basic type alias usage ✅
  - `tests/test_type_alias_expression_ret42.cpp` - Expression context usage ✅
  - `tests/test_enable_if_t_ret42.cpp` - Template alias usage ✅

**Current Status**: Type alias resolution now works. Headers transition from parsing errors to template instantiation timeouts, confirming that parsing correctness is resolved and performance optimization is the remaining blocker.


#### 0e. Structured Bindings (December 27, 2024)
**Status**: ✅ **NEWLY IMPLEMENTED**

**What Was Missing**: FlashCpp did not support C++17 structured bindings syntax (`auto [a, b] = expr;`), despite defining the `__cpp_structured_bindings` feature test macro. This prevented decomposition of structs, arrays, tuples, and pairs.

**The Problem**: 
- Parser did not recognize `auto [identifier-list]` pattern
- When encountering `[` after `auto`, parser would interpret it as array subscript
- Led to error: "Missing identifier" when trying to parse binding names
- No AST node existed for representing structured bindings
- No code generation support for decomposing objects

**Implementation**:
- **Parsing Phase** (src/Parser.cpp):
  - Modified `parse_type_and_name()` to detect `auto [` pattern
  - Added `parse_structured_binding()` function to parse identifier list
  - Supports all reference qualifiers: `auto [a, b]`, `auto& [a, b]`, `const auto& [a, b]`, `auto&& [a, b]`
  - Creates `StructuredBindingNode` AST node with binding identifiers and initializer

- **Code Generation Phase** (src/CodeGen.h):
  - Added `visitStructuredBindingNode()` to handle decomposition
  - Creates hidden variable to hold the initializer value
  - Supports two decomposition modes:
    1. **Struct decomposition**: Access public members by name and offset
    2. **Array decomposition**: Access elements by index
  - Validates identifier count matches struct member count or array size
  - Properly handles reference bindings (lvalue and rvalue)

**Test Cases**:
```cpp
// Struct decomposition - NOW WORKS!
struct Pair {
    int first;
    int second;
};

int main() {
    Pair p = {10, 32};
    auto [a, b] = p;  // ✅ Works!
    return a + b;  // Returns 42
}

// Array decomposition - NOW WORKS!
int main() {
    int arr[3] = {10, 20, 30};
    auto [x, y, z] = arr;  // ✅ Works!
    return x + y;  // Returns 30
}

// Reference qualifiers - NOW WORK!
auto& [a, b] = p;        // lvalue reference binding
const auto& [c, d] = p;  // const lvalue reference
auto&& [e, f] = Pair{};  // rvalue reference binding
```

**Impact**: 
- Enables modern C++17 code patterns for struct/array decomposition
- Unlocks use of structured bindings with standard library types like `std::pair` and `std::tuple` (once those are supported)
- Important for range-based for loops with structured bindings: `for (auto [key, value] : map)`

**Files Modified:**
- `src/Parser.cpp` - Added structured binding parsing
- `src/AstNodeTypes.h` - Added StructuredBindingNode class
- `src/CodeGen.h` - Added structured binding code generation
- Test files:
  - `tests/test_structured_binding_simple_ret42.cpp` - Basic struct decomposition ✅
  - `tests/test_structured_binding_array_ret30.cpp` - Array decomposition ✅
  - `tests/test_structured_binding_lvalue_ref_ret52.cpp` - Reference bindings ✅
  - `tests/test_structured_binding_invalid_static_fail.cpp` - Error case validation ✅

**Limitations**:
- Does not yet support tuple-like decomposition via `get<>()` (for std::tuple, std::pair when included from standard library)
- Does not support binding to bit-fields
- Does not support structured bindings in function parameters

**Next Steps**: The structured binding feature is complete for basic use cases. Future work could add tuple protocol support once standard library headers are fully operational.


#### 0. Qualified Base Class Names and Pack Expansion (December 26, 2024)
**Status**: ✅ **NEWLY IMPLEMENTED**

**What Was Missing**: FlashCpp could not parse qualified base class specifications or expand variadic packs in decltype base classes.

**Implementation**: 
- **Phase 2**: Added qualified base class name parsing
  - Support for `namespace::class` patterns
  - Support for `namespace::Template<Args>` patterns
  - Support for member type access (`::type`)
  - StringBuilder and StringHandle optimization
- **Phase 3**: Added pack expansion in decltype base classes
  - Extended `ExpressionSubstitutor` to handle pack parameters
  - Implemented pack detection and expansion logic
  - Support for mixed scalar and pack parameters
  - StringHandle keys in pack_map for consistency
- **Phase 4**: Added integration tests
  - Validated all features work together
  - Type alias resolution works correctly
  - Namespace template lookup works correctly

**Test Cases**:
```cpp
// Qualified base class with member type access
template<typename T>
struct wrapper : detail::select_base<T>::type { };

// Pack expansion in decltype base
template<typename... Bn>
struct logical_or : decltype(detail::or_fn<Bn...>(0)) { };
```

**Impact**: **Unblocked major `<type_traits>` patterns!** 🎉 The core template metaprogramming patterns now work.

**Files Modified:**
- `src/Parser.cpp` - Base class parsing updates
- `src/ExpressionSubstitutor.h` - Pack expansion support
- `src/ExpressionSubstitutor.cpp` - Implementation
- Added comprehensive integration tests

#### 1. Functional-Style Type Conversions (December 25, 2024)
**Status**: ✅ **IMPLEMENTED**

**What Was Missing**: FlashCpp did not support functional-style casts like `bool(x)`, `int(y)`, which are heavily used in standard library metaprogramming.

**Implementation**: 
- Added parsing in `parse_primary_expression()` for functional-style casts
- Created helper function `get_builtin_type_info()` to consolidate type mapping logic
- Handles both keyword types (`bool`, `int`, `float`, etc.) and user-defined types
- Works in template argument contexts

**Test Cases**:
```cpp
int x = 42;
bool b = bool(x);  // ✅ Now works!

// In template metaprogramming (the key blocker):
template<typename B>
auto fn() -> enable_if_t<!bool(B::value)>;  // ✅ Works!
```

**Impact**: **This unblocked `<type_traits>` header compilation!** 🎉 Since most C++ standard library headers include `<type_traits>`, this is a major breakthrough.

#### 1. Conversion Operators (FIXED)
**Status**: ✅ **Working correctly**  
**Previous Issue**: Conversion operators were using `void` as return type instead of target type  
**Fix**: Modified Parser.cpp to use the parsed target type directly as the return type  
**Tests**: 
- `test_conversion_operator_ret42.cpp` ✅
- `test_conversion_simple_ret42.cpp` ✅

**Impact**: Now conversion operators like `operator int()` correctly return `int` instead of `void`, enabling proper type conversions.

#### 2. Compiler Intrinsic: __builtin_addressof
**Status**: ✅ **Newly Implemented**  
**Description**: Returns the actual address of an object, bypassing any overloaded `operator&`  
**Implementation**: Added special parsing in Parser.cpp to handle `__builtin_addressof(expr)` syntax  
**Test**: `test_builtin_addressof_ret42.cpp` ✅

**Impact**: Essential for implementing `std::addressof` and related standard library functions.

**Status**: ✅ **FULLY IMPLEMENTED** (December 2024)
- Operator overload resolution now works correctly for `operator&`
- Regular `&` calls overloaded `operator&` if it exists
- `__builtin_addressof` always bypasses overloads (standard-compliant behavior)
- Infrastructure ready for extending to other operators (++, --, +, -, etc.)

**Implementation Details:**
- Added `is_builtin_addressof` flag to UnaryOperatorNode to distinguish `__builtin_addressof` from regular `&`
- Added `findUnaryOperatorOverload()` function in OverloadResolution.h to detect operator overloads
- Parser marks `__builtin_addressof` calls with the special flag
- CodeGen generates proper member function calls when overload is detected
- Sets `is_member_function = true` in CallOp for correct 'this' pointer handling

**Tests**: 
- `test_builtin_addressof_ret42.cpp` ✅ - Confirms __builtin_addressof bypasses overloads
- `test_operator_addressof_counting_ret42.cpp` ✅ - Demonstrates operator& being called
- Both behaviors now work correctly and independently

#### 3. Type Traits Intrinsics
**Status**: ✅ **Already Implemented** (verified during analysis)

The following type traits intrinsics are fully implemented and working:
- `__is_same(T, U)` - Check if two types are identical
- `__is_base_of(Base, Derived)` - Check inheritance relationship  
- `__is_class(T)` - Check if type is a class/struct
- `__is_enum(T)` - Check if type is an enum
- `__is_union(T)` - Check if type is a union
- `__is_polymorphic(T)` - Check if class has virtual functions
- `__is_abstract(T)` - Check if class has pure virtual functions
- `__is_final(T)` - Check if class/function is final
- `__is_pod(T)` - Check if type is Plain Old Data
- `__is_trivially_copyable(T)` - Check if type can be memcpy'd
- `__is_void(T)`, `__is_integral(T)`, `__is_floating_point(T)`
- `__is_pointer(T)`, `__is_reference(T)`, `__is_lvalue_reference(T)`, `__is_rvalue_reference(T)`
- `__is_array(T)`, `__is_const(T)`, `__is_volatile(T)`
- `__is_signed(T)`, `__is_unsigned(T)`
- And many more...

**Test**: `test_type_traits_intrinsics_working_ret235.cpp` ✅

#### 4. Additional Compiler Intrinsics (December 2024)
**Status**: ✅ **Newly Implemented**  
**Description**: Implemented four critical compiler intrinsics required by standard library headers

- `__builtin_unreachable` - Optimization hint that code path is unreachable
  - **Use case**: After switch default cases, after noreturn functions
  - **Test**: `test_builtin_unreachable_ret10.cpp` ✅
  
- `__builtin_assume(condition)` - Optimization hint that condition is true
  - **Use case**: Help optimizer with complex conditional logic
  - **Test**: `test_builtin_assume_ret42.cpp` ✅
  
- `__builtin_expect(expr, expected)` - Branch prediction hint
  - **Use case**: `if (__builtin_expect(rare_case, 0))` for unlikely branches
  - **Test**: `test_builtin_expect_ret42.cpp` ✅
  
- `__builtin_launder(ptr)` - Pointer optimization barrier
  - **Use case**: Essential for `std::launder`, placement new operations
  - **Test**: `test_builtin_launder_ret42.cpp` ✅

**Implementation**: Added intrinsic detection and inline IR generation in CodeGen.h  
**Impact**: These intrinsics are used extensively in `<memory>`, `<utility>`, and other headers for optimization and correctness

#### 5. Implicit Conversion Sequences (December 2024)
**Status**: ✅ **Fully Implemented**  
**Description**: Conversion operators now work in all contexts including variable initialization, function arguments, and return statements

**What Works**:
- Variable initialization with conversion operators: `int i = myStruct;` ✅
- Function arguments with implicit conversion: `func(myStruct)` where func expects different type ✅
- Return statements with implicit conversion: `return myStruct;` where return type differs ✅
- Conversion operators are automatically called when type conversion is needed
- Proper `this` pointer handling and member function call generation

**Implementation Details**:
- Added `findConversionOperator()` helper that searches struct and base classes
- Modified `visitVariableDeclarationNode()` to detect when conversion is needed
- Generates proper IR: takes address of source, calls conversion operator, assigns result

**Tests**:
- `test_conversion_simple_ret42.cpp` ✅
- `test_conversion_operator_ret42.cpp` ✅
- `test_conversion_add_ret84.cpp` ✅
- `test_conversion_comprehensive_ret84.cpp` ✅
- `test_implicit_conversion_fails.cpp` ✅
- `test_implicit_conversion_arg_ret42.cpp` ✅
- `test_implicit_conversion_return_ret42.cpp` ✅

**Example IR Generated**:
```
%mi = alloc 32
constructor_call MyInt %mi 42
%3 = addressof [21]32 %mi          ← Take address of source object
%2 = call @_ZN5MyInt12operator intEv(64 %3)  ← Call conversion operator
%i = alloc int32
assign %i = %2                     ← Assign result to target
ret int32 %i
```

**Impact**: Enables full automatic type conversion support, essential for standard library compatibility where implicit conversions are heavily used (e.g., `std::integral_constant::operator T()`).

### ⚠️ Known Limitations

#### 1. Static Constexpr Members in Templates
**Status**: ⚠️ **Known Issue**  
**Issue**: Accessing static constexpr members in template classes can cause crashes
**Example**:
```cpp
template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
};
bool b = integral_constant<int, 42>::value;  // May crash
```
**Impact**: Prevents full `std::integral_constant` pattern from working  
**Next Steps**: Requires improvements to constexpr evaluation and template instantiation

#### 2. Template Instantiation Performance
**Status**: ⚠️ **Known Issue**  
**Issue**: Complex template instantiation causes 10+ second timeouts  
**Impact**: Prevents compilation of full standard headers  
**Next Steps**: Requires template instantiation caching and optimization

### 📊 Current Standard Library Compatibility

- **Type Traits**: ✅ 80-90% compatible (most intrinsics working)
- **Utility Types**: ⚠️ 40-50% compatible (conversion operators fixed, but implicit conversions limited)
- **Containers**: ❌ Not supported (need allocators, exceptions, advanced features)
- **Algorithms**: ❌ Not supported (need iterators, concepts, ranges)
- **Strings/IO**: ❌ Not supported (need exceptions, allocators, locales)

---

## Critical Missing Features (High Priority)

These features are fundamental blockers for most standard library headers:

### Recently Completed (December 2024) ✅

The following critical features have been implemented:

1. **Conversion Operators** ✅ - Fully implemented, enables `<type_traits>` functionality
2. **Advanced constexpr Support** ✅ - C++14 control flow (loops, if/else, assignments) completed
3. **Static Constexpr Member Functions** ✅ - Can now be called in constexpr context
4. **Type Traits Intrinsics** ✅ - 30+ intrinsics verified working
5. **Compiler Intrinsics** ✅ - `__builtin_addressof`, `__builtin_unreachable`, `__builtin_assume`, `__builtin_expect`, `__builtin_launder`
6. **Implicit Conversion Sequences** ✅ - Working in all contexts
7. **Operator Overload Resolution** ✅ - Unary and binary operators fully supported
8. **Pack Expansion in decltype** ✅ - Enables complex template metaprogramming patterns
9. **Qualified Base Class Names** ✅ - Support for `ns::Template<Args>::type` patterns
10. **Member Type Access in Base Classes** ✅ - NEW (December 27) - Patterns like `__not_<T>::type` now parse correctly
11. **Structured Bindings** ✅ - **NEWLY COMPLETED** (December 27, 2024) - Full C++17 support with reference qualifiers
12. **Type Alias Resolution in Expression Contexts** ✅ - **NEWLY COMPLETED** (December 27, 2024) - Type aliases like `false_type`, `true_type` now resolve in all contexts

See "Recent Progress (December 2024)" section below for detailed implementation notes.

### Remaining Critical Features

### 1. Exception Handling Infrastructure
**Status**: ⚠️ **In Progress** (December 2024) - Linux implementation significantly advanced  
**Required by**: `<string>`, `<vector>`, `<iostream>`, `<memory>`, and most containers

**Recent Progress (December 30, 2024):**
- ✅ Parser support (try/catch/throw/noexcept) - Complete
- ✅ IR instructions (TryBegin, CatchBegin, Throw, Rethrow) - Complete
- ✅ LSDA (Language Specific Data Area) generation - Implemented with 13+ bug fixes
- ✅ .eh_frame section with CFI instructions - Complete
- ✅ __cxa_throw/__cxa_begin_catch/__cxa_end_catch calls - Generated
- ✅ Type info symbols (_ZTIi, etc.) - External references to C++ runtime
- ✅ Literal exception values (`throw 42`) - Now works correctly
- ⚠️ Runtime still has .eh_frame parsing issues - Under investigation
- ❌ Windows SEH - Not implemented yet

**Still Missing**:
- Runtime exception catching (LSDA/personality routine interaction)
- RTTI integration for complex exception type matching (see `docs/EXCEPTION_HANDLING_PLAN.md`)
- Windows MSVC SEH implementation
- Standard exception classes hierarchy
- Exception-safe constructors/destructors

**Impact**: Critical - Most standard library code uses exceptions  
**Files affected**: `test_std_string.cpp`, `test_std_vector.cpp`, `test_std_iostream.cpp`, `test_std_memory.cpp`

**Documentation**: See `docs/EXCEPTION_HANDLING_PLAN.md` for detailed implementation roadmap

### 2. Allocator Support
**Status**: ❌ Not implemented  
**Required by**: All container headers (`<vector>`, `<string>`, `<map>`, `<set>`, etc.)

**Missing features**:
- std::allocator and allocator_traits
- Custom allocator template parameters
- Allocator-aware constructors and operations
- Memory resource management

**Impact**: Critical - All standard containers use allocators  
**Files affected**: `test_std_vector.cpp`, `test_std_string.cpp`, `test_std_map.cpp`, `test_std_set.cpp`

### 3. Template Instantiation Performance
**Status**: ⚠️ Causes timeouts and hangs  
**Required by**: All standard library headers

**Issues**:
- Deep template instantiation depth causes performance issues
- Recursive template instantiation not optimized
- Template instantiation caching partially implemented but needs improvement
- SFINAE causes exponential compilation time

**Impact**: Critical - Causes 10+ second timeouts  
**Files affected**: All 16 timeout cases

**Note**: Profiling infrastructure exists (`--timing` flag), individual instantiations are fast (20-50μs), but volume is the issue.

### 4. Remaining constexpr Features
**Status**: ⚠️ Partially implemented  
**Required by**: Most C++20 headers including `<array>`, `<string_view>`, `<span>`, `<algorithm>`

**Still Missing**:
- Constexpr constructors and destructors in complex classes
- Constexpr evaluation of placement new and complex expressions
- Constexpr if with dependent conditions in some edge cases

**Impact**: High - Blocks some advanced compile-time computations  
**Files affected**: `test_std_array.cpp`, `test_std_string_view.cpp`, `test_std_span.cpp`

## Important Missing Features (Medium Priority)

### 6. Iterator Concepts and Traits
**Status**: Not implemented
**Required by**: `<algorithm>`, `<ranges>`, `<iterator>`, all containers

**Missing features**:
- Iterator category traits (input_iterator, forward_iterator, etc.)
- Iterator operations (advance, distance, next, prev)
- Iterator adaptors (reverse_iterator, move_iterator)
- Range-based iterator support

**Impact**: High - Required for algorithms and range-based code
**Files affected**: `test_std_algorithm.cpp`, `test_std_ranges.cpp`

### 7. C++20 Concepts
**Status**: Basic concept support exists, but incomplete
**Required by**: `<concepts>`, `<ranges>`, `<algorithm>`, `<iterator>`

**Missing features**:
- Requires clauses with complex expressions
- Nested concept requirements
- Concept subsumption rules
- Automatic constraint checking

**Impact**: High - C++20 headers heavily use concepts
**Files affected**: `test_std_concepts.cpp`, `test_std_ranges.cpp`, `test_std_algorithm.cpp`

### 8. Type Erasure and Virtual Dispatch
**Status**: Basic virtual functions work, but complex patterns don't
**Required by**: `<any>`, `<function>`, `<memory>` (unique_ptr with custom deleter)

**Missing features**:
- Small buffer optimization for type erasure
- Virtual destructor chaining in complex hierarchies
- Type ID and RTTI for std::any
- Function pointer wrappers with state

**Impact**: Medium-High - Affects modern C++ patterns
**Files affected**: `test_std_any.cpp`, `test_std_functional.cpp`, `test_std_memory.cpp`

### 9. Perfect Forwarding and Move Semantics
**Status**: Basic rvalue references work, but std::forward/std::move integration incomplete
**Required by**: `<utility>`, `<memory>`, all containers, `<functional>`

**Missing features**:
- Full std::forward implementation
- std::move optimization in all contexts
- Reference collapsing in complex templates
- Move-only types in containers

**Impact**: Medium-High - Core to modern C++
**Files affected**: `test_std_utility.cpp`, `test_std_memory.cpp`, all containers

### 10. Advanced SFINAE and Template Metaprogramming
**Status**: ✅ **SFINAE patterns now work correctly** (December 2024)
**Required by**: `<type_traits>`, `<functional>`, `<tuple>`, `<variant>`

**Implemented features**:
- std::enable_if in complex contexts
- **Void_t pattern SFINAE** ✅ **FIXED** (December 2024) - See below
- Detection idiom support
- Tag dispatch patterns

**Impact**: Critical for library implementation patterns - now working!
**Files affected**: `test_std_type_traits.cpp`, `test_std_functional.cpp`, `test_std_tuple.cpp`

#### Void_t SFINAE Fix (December 2024)
**Status**: ✅ **FULLY WORKING** - Both positive and negative cases now work correctly!

**The Problem Was**: FlashCpp had a limitation in void_t SFINAE pattern matching:
- Pattern matching happened BEFORE default template arguments were filled in
- This caused size mismatch between concrete args (1 arg) and pattern args (2 args)
- As a result, the specialization was never selected

**The Fix Implemented**:
1. **Default argument fill-in before pattern matching**: Added code to fill in default template arguments before calling `matchSpecializationPattern()`
2. **Auto-detection of void_t SFINAE patterns**: Patterns with 2 args where first is dependent and second is void, and the struct inherits from `true_type`, are auto-detected as void_t detection patterns
3. **SFINAE condition check during pattern matching**: Added `SfinaeCondition` struct to store member type requirements, and modified `TemplatePattern::matches()` to verify the condition is satisfied
4. **Member type alias registration with qualified names**: Fixed `parse_member_type_alias()` to register type aliases with qualified names (e.g., `WithType::type` instead of just `type`)

**Both Cases Now Work:**
```cpp
struct WithType { using type = int; };    // Has 'type' member
struct WithoutType { int value; };         // No 'type' member

// Positive Case (type WITH 'type' member):
has_type<WithType>::value    // Returns true ✅ (specialization matches)

// Negative Case (type WITHOUT 'type' member):
has_type<WithoutType>::value // Returns false ✅ (primary template used via SFINAE)
```

**Test Files**:
- `tests/test_void_t_positive_ret0.cpp` - Tests positive case (type WITH `type` member) - Returns 0 ✅
- `tests/test_void_t_detection_ret42.cpp` - Tests negative case (type WITHOUT `type` member) - Returns 42 ✅

## Advanced Missing Features (Lower Priority)

### 11. Variadic Template Expansion in Complex Contexts
**Status**: Basic variadic templates work, but complex expansions timeout
**Required by**: `<tuple>`, `<variant>`, `<functional>`, fold expressions

**Impact**: Medium - Affects tuple-like types
**Files affected**: `test_std_tuple.cpp`, `test_std_variant.cpp`

### 12. Complex Union Handling
**Status**: Basic unions work
**Required by**: `<variant>`, `<optional>`

**Missing features**:
- Unions with non-trivial member types
- Active member tracking
- Discriminated union patterns

**Impact**: Medium - Specific to variant/optional
**Files affected**: `test_std_variant.cpp`, `test_std_optional.cpp`

### 13. Locales and Facets
**Status**: Not implemented
**Required by**: `<iostream>`, `<string>`, I/O manipulators

**Impact**: Low-Medium - Can be stubbed for basic I/O
**Files affected**: `test_std_iostream.cpp`

### 14. Ranges and Views (C++20)
**Status**: Not implemented
**Required by**: `<ranges>`, modern `<algorithm>`

**Missing features**:
- Range adaptors and pipeable views
- Lazy evaluation framework
- Range-based algorithms
- View composition

**Impact**: Low-Medium - C++20 specific feature
**Files affected**: `test_std_ranges.cpp`

### 15. Chrono Arithmetic and Ratio Templates
**Status**: Not implemented
**Required by**: `<chrono>`

**Missing features**:
- std::ratio template
- Duration arithmetic
- Time point operations
- Clock abstractions

**Impact**: Low-Medium - Time-specific functionality
**Files affected**: `test_std_chrono.cpp`

## Compiler Intrinsics Needed

The standard library relies heavily on compiler intrinsics for efficiency:

### Type Traits Intrinsics (✅ Implemented)
```cpp
__is_same(T, U)
__is_base_of(Base, Derived)
__is_class(T)
__is_enum(T)
__is_union(T)
__is_pod(T)
__is_trivially_copyable(T)
__is_polymorphic(T)
__is_abstract(T)
__is_final(T)
__is_aggregate(T)
__has_virtual_destructor(T)
```

### Other Intrinsics (✅ Implemented)
```cpp
__builtin_addressof      // ✅ Returns actual address, bypassing operator&
__builtin_launder        // ✅ Optimization barrier for pointers (placement new)
__builtin_unreachable    // ✅ Marks code path as unreachable
__builtin_assume         // ✅ Assumes condition is true (optimization hint)
__builtin_expect         // ✅ Branch prediction hint
```

## Preprocessor and Feature Test Macros

Standard headers check for many feature test macros. FlashCpp now defines:

**Language Feature Macros:**
```cpp
__cpp_exceptions
__cpp_rtti
__cpp_static_assert
__cpp_decltype
__cpp_auto_type
__cpp_nullptr
__cpp_lambdas
__cpp_range_based_for
__cpp_variadic_templates
__cpp_initializer_lists
__cpp_delegating_constructors
__cpp_constexpr
__cpp_if_constexpr
__cpp_inline_variables
__cpp_structured_bindings
__cpp_noexcept_function_type
__cpp_concepts
__cpp_aggregate_bases
```

**Library Feature Macros (New in December 2024):**
```cpp
__cpp_lib_type_trait_variable_templates  // ✅ C++17 type traits as variables
__cpp_lib_addressof_constexpr           // ✅ C++17 constexpr addressof
__cpp_lib_integral_constant_callable    // ✅ C++14 integral_constant::operator()
__cpp_lib_is_aggregate                  // ✅ C++17 is_aggregate
__cpp_lib_void_t                        // ✅ C++17 void_t
__cpp_lib_bool_constant                 // ✅ C++17 bool_constant
```

**Compiler Builtin Detection (January 2, 2026):**
```cpp
__has_builtin(x)  // ✅ Detects if a compiler builtin is supported (NEW!)
                  // Supported for 60+ type trait and builtin function intrinsics
```

**Attribute Detection Macros:**
```cpp
__has_cpp_attribute(nodiscard)
__has_cpp_attribute(deprecated)
```

These macros enable conditional compilation in standard library headers based on FlashCpp's feature support.

## Performance Issues

### Compilation Speed
- Template instantiation causes 10+ second timeouts
- Need template instantiation caching
- Need early template parsing optimization
- Consider incremental compilation support

### Memory Usage
- Deep template recursion may cause memory issues
- Need to limit instantiation depth
- Consider template instantiation memoization

## Recommended Implementation Order

To enable standard library support, implement features in this order:

1. ~~**Conversion operators**~~ ✅ **COMPLETED** - Unlocks `<type_traits>`
2. ~~**Improved constexpr**~~ ✅ **PARTIALLY COMPLETED** (December 23, 2024) - C++14 constexpr control flow now works
3. **Template instantiation optimization** - Reduces timeouts (HIGHEST PRIORITY NOW)
4. ~~**Type traits intrinsics**~~ ✅ **COMPLETED** - Speeds up `<type_traits>` compilation
5. **Exception handling completion** - Unlocks containers
6. **Allocator support** - Unlocks `<vector>`, `<string>`, `<map>`, `<set>`
7. **Iterator concepts** - Unlocks `<algorithm>`, `<ranges>`
8. **C++20 concepts completion** - Unlocks modern headers
9. **Type erasure patterns** - Unlocks `<any>`, `<function>`
10. **Advanced features** - Locales, chrono, ranges

### Next Immediate Priorities

Based on recent progress (December 27, 2024):

1. ~~**Immediate**: Fix static constexpr member access in templates~~ ✅ **FIXED** (commit 6bae992) - Static member functions work in constexpr
2. ~~**Immediate**: Implement missing compiler intrinsics~~ ✅ **COMPLETED** - All 4 critical intrinsics implemented
3. ~~**Short-term**: Implement implicit conversion sequences~~ ✅ **FULLY COMPLETED** - Working in all contexts
4. ~~**Short-term**: Implement operator overload resolution~~ ✅ **WORKING** - Tests confirm most operators work correctly
5. ~~**Short-term**: Expand constexpr control flow support~~ ✅ **COMPLETED** (commit 6458c39) - For loops, while loops, if/else, assignments, increments
6. ~~**Short-term**: Implement structured bindings~~ ✅ **COMPLETED** (December 27, 2024, commit 2c5f5f3) - Full C++17 support with reference qualifiers
7. **Medium-term**: **Optimize template instantiation for performance** ← CURRENT HIGHEST PRIORITY
8. **Medium-term**: Complete remaining constexpr features (constructors, complex expressions)
9. **Long-term**: Add allocator and exception support for containers

## Testing Strategy

### Incremental Testing
After implementing each feature:
1. Run `tests/test_std_headers_comprehensive.sh`
2. Check which headers now compile
3. Move successfully compiling headers out of EXPECTED_FAIL
4. Document any new issues discovered

### Minimal Test Headers
Consider creating minimal versions of standard headers for testing:
- `<mini_type_traits>` - Just integral_constant and is_same
- `<mini_vector>` - Just vector without allocators
- `<mini_string>` - Just basic_string without locales

This allows testing individual features without full standard library complexity.

## Practical Workarounds for Using FlashCpp Today

### Create Your Own Simplified Standard Library Components

Since full standard headers timeout, you can create lightweight versions for your projects:

**Example: Minimal Type Traits**
```cpp
// my_type_traits.h
namespace my_std {
    template<typename T, T v>
    struct integral_constant {
        static constexpr T value = v;
        constexpr operator T() const noexcept { return value; }
    };
    
    template<bool B>
    using bool_constant = integral_constant<bool, B>;
    
    using true_type = bool_constant<true>;
    using false_type = bool_constant<false>;
    
    template<typename T, typename U>
    struct is_same : false_type {};
    
    template<typename T>
    struct is_same<T, T> : true_type {};
    
    template<typename T, typename U>
    inline constexpr bool is_same_v = is_same<T, U>::value;
}
```

**Example: Simplified Optional**
```cpp
// my_optional.h - Note: This is a simplified runtime-only version
template<typename T>
class optional {
    bool has_val;
    alignas(T) char storage[sizeof(T)];
    
public:
    optional() : has_val(false) {}
    
    optional(const T& val) : has_val(true) {
        new (storage) T(val);  // Placement new (not constexpr-compatible)
    }
    
    bool has_value() const { return has_val; }
    
    T& value() { 
        return *reinterpret_cast<T*>(storage);  // Not constexpr-compatible
    }
};
```

**Tips:**
- Use FlashCpp's type trait intrinsics directly: `__is_same(T, U)`, `__is_class(T)`, etc.
- Avoid complex template metaprogramming patterns that cause deep instantiation
- Keep constexpr functions simple (basic arithmetic and logic only)
- Don't rely on allocators or exceptions yet

## Conclusion

Supporting standard library headers is a complex undertaking requiring many advanced C++ features. 

### Recently Completed (December 2024)
✅ Conversion operators - Now return correct types  
✅ Type traits compiler intrinsics - 30+ intrinsics verified working  
✅ `__builtin_addressof` - Essential for `std::addressof`  
✅ Additional compiler intrinsics - `__builtin_unreachable`, `__builtin_assume`, `__builtin_expect`, `__builtin_launder`  
✅ Implicit conversion sequences - **FULLY WORKING** - Conversion operators now called automatically in all contexts:
  - Variable initialization: `int i = myStruct;` ✅
  - Function arguments: `func(myStruct)` where func expects different type ✅
  - Return statements: `return myStruct;` where return type differs ✅
✅ Library feature test macros - Added 6 standard library feature detection macros (`__cpp_lib_*`)
✅ **Unary operator overload resolution** - **FULLY COMPLETED** (December 2024):
  - Regular `&` calls `operator&` overload if it exists ✅
  - `__builtin_addressof` always bypasses overloads ✅
  - Proper member function call generation with 'this' pointer ✅
  - Tests confirm unary operators (++, --, *, &, ->) work correctly ✅
✅ **Binary operator overload resolution** - **FULLY COMPLETED** (December 23, 2024):
  - Binary operators (+, -, *, /, %, ==, !=, <, >, <=, >=, &&, ||, &, |, ^, <<, >>) now call overloaded member functions ✅
  - Automatic address-taking for reference parameters ✅
  - Proper return value handling for struct-by-value returns (RVO/NRVO) ✅
  - Test: `test_operator_plus_overload_ret15.cpp` ✅
  - Impact: Essential for custom numeric types, smart pointers, iterators, and standard library patterns ✅
✅ **Static constexpr member functions** - **FIXED** (December 23, 2024, commit 6bae992):
  - Static member functions can now be called in constexpr context ✅
  - `Point::static_sum(5, 5)` works in static_assert ✅
  - Unblocks `std::integral_constant<T,V>::value` patterns ✅
  - Test: `test_static_constexpr_member_ret42.cpp` ✅
✅ **C++14 Constexpr control flow** - **IMPLEMENTED** (December 23, 2024, commit 6458c39):
  - For loops with init, condition, update ✅
  - While loops ✅
  - If/else statements (including C++17 if-with-init) ✅
  - Assignment operators (=, +=, -=, *=, /=, %=) ✅
  - Increment/decrement (++, --, prefix and postfix) ✅
  - Tests: `test_constexpr_control_flow_ret30.cpp`, `test_constexpr_loops.cpp` ✅
✅ **Structured Bindings** - **IMPLEMENTED** (December 27, 2024, commit 2c5f5f3):
  - Full C++17 structured binding support with all reference qualifiers ✅
  - Struct decomposition: `auto [a, b] = pair;` ✅
  - Array decomposition: `auto [x, y, z] = arr;` ✅
  - Reference bindings: `auto&`, `const auto&`, `auto&&` ✅
  - Tests: `test_structured_binding_simple_ret42.cpp`, `test_structured_binding_array_ret30.cpp` ✅
  - Impact: Enables modern C++17 decomposition patterns ✅

### Most Impactful Next Steps
1. ~~Fix static constexpr member access in templates~~ ✅ **FIXED** (commit 6bae992) - Enables `std::integral_constant`
2. ~~Implement implicit conversion sequences~~ ✅ **FULLY COMPLETED** - Enables automatic type conversions in all contexts
3. ~~Add library feature test macros~~ ✅ **COMPLETED** - Enables conditional compilation in standard headers
4. ~~Complete operator overload resolution~~ ✅ **FULLY COMPLETED** (commit e2c874a) - All unary and binary operators work
5. ~~Expand constexpr control flow support~~ ✅ **COMPLETED** (commit 6458c39) - For loops, while loops, if/else, assignments
6. ~~Implement structured bindings~~ ✅ **COMPLETED** (commit 2c5f5f3) - Full C++17 support with reference qualifiers
7. ~~Fix type alias resolution in expression contexts~~ ✅ **COMPLETED** (commit 29449d1) - Type aliases like false_type, true_type now resolve
8. **Optimize template instantiation** ← **HIGHEST PRIORITY NOW** - Reduces timeouts, main blocker for headers
9. Complete remaining constexpr features (constructors, complex expressions)

Once template optimization is implemented, simpler headers like `<type_traits>`, `<array>`, and `<span>` should compile successfully.

---

## Current Work Plan (December 23, 2024)

### ✅ Priority 1: Binary Operator Overload Resolution - COMPLETED

**Status**: ✅ **FULLY IMPLEMENTED** (December 23, 2024)  
**Issue**: Binary arithmetic operators on struct types were generating built-in IR instead of calling overloaded operator functions  
**Solution**: Implemented complete binary operator overload resolution

#### Implementation Details:

**1. Added `findBinaryOperatorOverload()` in `OverloadResolution.h`:**
- Searches for binary operator member functions in struct types (e.g., `operator+`, `operator-`, etc.)
- Supports recursive search through base classes
- Returns operator overload result with member function information
- Handles both const and non-const overloads

**2. Modified `generateBinaryOperatorIr()` in `CodeGen.h`:**
- Checks for operator overloads before generating built-in arithmetic operations
- Takes address of LHS to pass as 'this' pointer for member function calls
- Automatically detects if parameters are references and takes address when needed
- Generates proper mangled function names for operator calls
- Sets `uses_return_slot` flag for struct-by-value returns (RVO/NRVO)
- Sets `return_type_index` for proper type tracking
- Generates `FunctionCall` IR instruction with all necessary metadata

**3. Supported Operators:**
- ✅ Arithmetic: `+`, `-`, `*`, `/`, `%`
- ✅ Comparison: `==`, `!=`, `<`, `>`, `<=`, `>=`
- ✅ Logical: `&&`, `||`
- ✅ Bitwise: `&`, `|`, `^`
- ✅ Shift: `<<`, `>>`
- ✅ Spaceship: `<=>` (already implemented separately)

**4. Test Results:**
- ✅ `test_operator_plus_overload_ret15.cpp` - **PASSING** (returns 15 for 5 + 10)
- ✅ `test_operator_arrow_overload_ret100.cpp` - **ALREADY WORKING** (returns 100)
- ✅ All other operator tests continue to pass
- ✅ No regressions introduced

**Example Generated IR:**
```
// For: Number c = a + b;
%5 = addressof [21]32 %a           // Take address of LHS ('this')
%6 = addressof [21]32 %b           // Take address of RHS (reference param)
%4 = call @_ZN6Number9operator+ERK6Number(64 %5, 64 %6)  // Member function call
%c = alloc 32
assign %c = %4
```

**Files Modified:**
- `src/OverloadResolution.h` - Added `findBinaryOperatorOverload()` function
- `src/CodeGen.h` - Modified `generateBinaryOperatorIr()` to check for overloads

**Impact**: Binary operator overloads now work correctly for all arithmetic, comparison, logical, bitwise, and shift operators. This is essential for custom numeric types, smart pointers, iterators, and other operator-based patterns used throughout the standard library.

---

## Next Priority: Template Instantiation Performance (CRITICAL)
**Status**: ✅ **Profiling Infrastructure Completed** (December 23, 2024)  
**Issue**: Complex template instantiation causes 10+ second timeouts

### ✅ Completed: Performance Profiling Infrastructure

**Implementation Details:**
- ✅ Created comprehensive `TemplateProfilingStats.h` profiling system
- ✅ Added RAII-based profiling timers with zero overhead when disabled
- ✅ Instrumented key template instantiation functions in `Parser.cpp`
- ✅ Integrated with `--timing` command line flag
- ✅ Created detailed documentation in `docs/TEMPLATE_PROFILING.md`

**Tracked Metrics:**
1. **Template instantiation counts and timing** - Per-template breakdown
2. **Cache hit/miss rates** - Currently ~26% on test cases
3. **Operation breakdowns**:
   - Template lookups: < 1 microsecond
   - Specialization matching: 1-8 microseconds
   - Full instantiation: 20-50 microseconds
4. **Top 10 reports** - Most instantiated and slowest templates

**Key Findings:**
- Individual template instantiations are quite fast (20-50μs)
- Cache hit rate has room for improvement
- Standard header timeouts are due to **volume** not individual template speed
- Existing optimizations (caching, depth limits) are working correctly

**Usage:**
```bash
./FlashCpp myfile.cpp -o myfile.o --timing
```

**Files Modified:**
- ✅ `src/TemplateProfilingStats.h` - Profiling infrastructure (new)
- ✅ `src/Parser.cpp` - Instrumented instantiation functions
- ✅ `src/main.cpp` - Integration with --timing flag
- ✅ `docs/TEMPLATE_PROFILING.md` - Comprehensive documentation (new)

### 🔄 Next Steps: Performance Optimization

**Immediate Priorities:**
1. **Profile standard library headers** - Use profiling to identify specific bottlenecks in real headers
2. **Optimize based on data** - Target the actual slowest operations
3. **Improve cache hit rates** - Current ~26% has room for improvement

**Potential Optimizations (Data-Driven):**
1. **String operation optimization** - Template name generation uses concatenation
2. **Type resolution caching** - Cache frequently-used type lookups
3. **Instantiation batching** - Batch independent instantiations

**Already Implemented Optimizations:**
- ✅ Template instantiation caching (both class and function templates)
- ✅ Recursion depth limit (10 levels)
- ✅ Early return for dependent types
- ✅ Pattern-based specialization matching
- ✅ **Lazy member instantiation** (December 2024) - Template member functions are only instantiated when actually called, not when the class template is instantiated. Implemented via `LazyMemberInstantiationRegistry` and `LazyMemberFunctionTemplateRegistry`.

### Performance Measurement Results

**Test Case: 19 Template Instantiations**
```
Overall Breakdown:
  Template Lookups        : count=14, total=0.001 ms, mean=0.071 μs
  Specialization Matching : count=14, total=0.018 ms, mean=1.286 μs

Cache Statistics:
  Cache Hits:   5
  Cache Misses: 14
  Hit Rate:     26.32%

Top Templates:
  1. Wrapper : count=9, total=0.433 ms, mean=48.111 μs
  2. Pair    : count=6, total=0.163 ms, mean=27.167 μs
  3. Triple  : count=4, total=0.116 ms, mean=29.000 μs
```

**Analysis:**
- Fast operations: Lookups (<1μs), matching (1-8μs)
- Moderate: Instantiation (20-50μs per template)
- Volume issue: Standard headers likely have 100s-1000s of instantiations
- Cache opportunity: Only 26% hit rate, could be improved

---

## Latest Updates (December 25, 2024)

### ✅ Parsing Improvements - December 25, 2024 (Evening - Part 2)

**Status**: ✅ **PACK EXPANSION IN TEMPLATE ARGUMENTS IMPLEMENTED**

#### 3. Pack Expansion in Template Arguments

**Issue**: Standard library headers use pack expansion (`...`) in template argument expressions, which was not supported in template argument contexts.

**What Was Fixed:**
- Modified `parse_explicit_template_arguments()` to accept `...` as valid token after expressions
- Added pack expansion handling for:
  - Boolean literals (e.g., `true...`)
  - Numeric literals (e.g., `42...`)
  - Constant expressions in SFINAE context
  - Dependent expressions in template declaration context (e.g., `!bool(Bn::value)...`)
- Template arguments are now properly marked with `is_pack = true` when followed by `...`

**Example Now Working:**
```cpp
template<typename... Bn>
struct test {
    // Pack expansion in template argument - the pattern from <type_traits> line 175
    template<typename T>
    using result = enable_if<!bool(Bn::value)...>;
};
```

**Impact**: Enables parsing of SFINAE patterns with pack expansion in template arguments, unblocking line 175 of `<type_traits>` header.

**Test**: `test_pack_expansion_template_args_ret42.cpp` ✅

### ✅ Parsing Improvements - December 25, 2024 (Evening - Part 1)

**Status**: ✅ **TWO NEW FEATURES IMPLEMENTED**

#### 1. ::template Keyword Support

**Issue**: Standard library headers use `::template` syntax to access member templates in dependent contexts, but FlashCpp didn't recognize this syntax.

**What Was Fixed:**
- Modified `parse_qualified_identifier_after_template()` to consume optional `template` keyword after `::`
- Added handling for template arguments following `::template member<Args>`
- Created dependent type placeholders that include template argument information

**Example Now Working:**
```cpp
template<typename T>
using conditional_t = typename Base<T>::template type<int, double>;
```

**Impact**: Enables parsing of complex template metaprogramming patterns used throughout the standard library.

**Test**: `test_template_parsing_ret42.cpp` ✅

#### 2. Auto Return Type with Trailing Return Type in Template Functions

**Issue**: Template functions with `auto` return type and trailing return type syntax were not supported, only regular functions had this feature.

**What Was Fixed:**
- Modified `parse_template_function_declaration_body()` to check for `->` after auto return type
- Parses trailing return type and replaces the auto type before checking trailing specifiers
- Ensures proper order: parameters → trailing return type → trailing specifiers → body

**Example Now Working:**
```cpp
template<typename T>
auto test_func(T x) -> decltype(x + 1) {
    return x + 1;
}
```

**Impact**: Critical for C++11/14 style generic programming with deduced return types.

**Test**: `test_auto_trailing_return_ret42.cpp` ✅

### 📊 Progress on <type_traits> Header

**Before Today:**
- Line 157: ❌ Failed with `error: Expected identifier after '::'`
- Could not parse `typename __conditional<_Cond>::template type<_If, _Else>;`

**After Fix 1 (::template support):**
- Line 157: ✅ **FIXED** - Now parses successfully
- Line 175: ❌ Failed with `error: Expected type specifier` at `auto __or_fn(int) ->`

**After Fix 2 (auto + trailing return):**
- Line 175: ✅ **IMPROVED** - Now accepts `->` and starts parsing return type
- Line 175: ❌ Blocker at column 38 - Complex template argument expression with pack expansion

**After Fix 3 (pack expansion in template arguments):**
- Line 175: ✅ **FIXED** - Pack expansion `!bool(_Bn::value)...` now parses correctly
- Line 194: ❌ **NEW BLOCKER** - `decltype` in base class specification

**Current Blocker Details:**
```cpp
template<typename... _Bn>
struct __or_
  : decltype(__detail::__or_fn<_Bn...>(0))
    { };
```

The issue is that `decltype` is not supported as a base class specifier. This requires significant changes to inheritance parsing logic.

This is advanced SFINAE and would require significant template metaprogramming support.

### ✅ Parsing Bug Fixes - noexcept Support

**Status**: ✅ **COMPLETED**

**Issue**: Standard library headers like `<limits>` failed to parse due to missing support for `noexcept` specifiers on static member functions in template classes.

**What Was Fixed:**
1. **Static member functions with noexcept** - Parser now properly handles trailing function specifiers (noexcept, const, volatile, ref-qualifiers) for static member functions
2. **Multi-line function declarations** - Handles return type on separate line from function name
3. **Template specializations** - Fix applied to 3 code paths:
   - Regular struct parsing
   - Full template specialization parsing  
   - Partial template specialization parsing

**Code Quality Improvements:**
- Refactored ~310 lines of duplicated code into reusable helper functions:
  - `parse_static_member_function()` - Handles function detection and parsing
  - `parse_static_member_block()` - Handles entire static member logic (functions + data)

**Example Now Working:**
```cpp
template<typename T>
struct numeric_limits {
    static constexpr T
    min() noexcept { return T(); }  // ✅ Now parses correctly
    
    static constexpr bool is_specialized = true;  // ✅ Data members work too
};
```

**Impact on Standard Headers:**
- Parsing errors resolved - headers no longer fail with syntax errors
- **However**: Headers still timeout (>10s) due to template instantiation volume
- This confirms: **parsing is fixed, performance is the remaining blocker**

### 🔄 Current Status: Performance Bottleneck

**Root Cause Identified:**
- Individual template instantiations are fast (20-50μs)
- Standard headers have **hundreds to thousands** of instantiations
- 100 templates × 50μs = 5ms (fast)
- 1000 templates × 50μs = 50ms (acceptable)  
- 10,000 templates × 50μs = 500ms (slow)
- Real headers likely exceed this with nested dependencies

**What's Working:**
- ✅ Lazy member instantiation (only instantiate members when called)
- ✅ Template caching (reuse instantiated templates)
- ✅ Profiling infrastructure (`--timing` flag)

**Remaining Opportunities:**
1. **Improve cache hit rates** (currently ~26%)
2. **Optimize string operations** in template name generation
3. **Type resolution caching** for frequently-used types
4. **Consider incremental compilation** or precompiled headers

**Recommended Approach:**
For immediate productivity, create simplified custom implementations of standard utilities rather than waiting for full stdlib support. The performance work required is substantial and beyond the scope of parsing fixes.

---

## Investigation Update (December 26, 2024)

### Comprehensive Analysis of Current Blocker

**Investigation completed** to identify root causes and verify feature status.

#### Verified Working Features ✅

Through runtime testing (not just compilation):
- **Conversion operators**: `test_conversion_simple_ret42.cpp` executes and returns 42 ✅
- **Constexpr control flow**: Factorial with for loops evaluates correctly at compile time ✅
- **Simple decltype bases**: Non-variadic cases work as documented ✅
- **Template instantiation caching**: Profiler confirms implementation is active ✅

#### Root Cause: Variadic Pack Expansion in Decltype

**Primary blocker** for `<type_traits>` line 194:

```cpp
template<typename... _Bn>
struct __or_ : decltype(__detail::__or_fn<_Bn...>(0)) { };
```

**Technical analysis:**
- `ExpressionSubstitutor` (src/ExpressionSubstitutor.cpp) handles simple template parameter substitution
- **Missing**: Parameter pack expansion (`_Bn...` → `B1, B2, B3, ...`) in expression contexts
- Line 127-271 shows template argument substitution logic exists
- Pack expansion requires AST traversal refactoring to expand packs during substitution
- **Complexity**: HIGH - affects multiple code paths, requires careful interaction between parsing, substitution, and instantiation phases

#### Secondary Issue: Type Alias Resolution

**Error observed**: `Missing identifier: false_type`

When compiling `<type_traits>`:
```
[ERROR][Parser] Missing identifier: false_type
[ERROR][Parser] Missing identifier: true_type
[ERROR][Parser] Missing identifier: __enable_if_t
```

**Analysis**: The error occurs in expression parsing context (Parser.cpp:14393) when identifierType lookup returns null. Type aliases are registered in `gTypesByName` (line 6315 in parse_using_directive_or_declaration), but identifier lookup for expressions only checks `gSymbolTable` (line 13365), not `gTypesByName`.

**Root cause**: When type aliases like `false_type` are used in expression contexts (not as base classes or constructor calls), the parser doesn't fall back to checking `gTypesByName`. The fallback at line 13613 only handles constructor calls (when followed by `(`).

**Potential fix**: Add a fallback in expression parsing to check `gTypesByName` for type aliases when symbol table lookup fails and the identifier is not followed by `(`. This would allow type aliases to be used in more contexts within template metaprogramming patterns.

#### Performance Analysis

Profiling with `--timing` flag shows:
```
<cstddef> compilation: 544ms total
  - Preprocessing: 98.2% (535ms)
  - Parsing: 1.4% (7.7ms)
  - Template instantiation: 0.0%
  - Templates instantiated: 1
```

**Conclusion**: Template instantiation performance is **not a blocker**. Individual instantiations are fast (20-50μs). The issue is **parsing correctness**, not performance.

#### Recommended Implementation Order

**High Priority (Achievable):**
1. ✅ Already done: Conversion operators, constexpr control flow, simple decltype
2. **Next**: Debug type alias resolution in template namespaces (separate from pack expansion)
3. Add comprehensive test cases for edge cases of working features

**Medium Priority (Complex):**
4. Implement parameter pack expansion in `ExpressionSubstitutor`
   - Start with single pack parameter cases
   - Gradually add complexity with multiple packs
   - Extensive testing required

**Low Priority:**
5. ~~Template instantiation performance optimization~~ - Already fast, not currently a blocker

---

**Last Updated**: December 26, 2024 (Investigation Update)
**Recent Contributors**: GitHub Copilot, FlashCpp team
