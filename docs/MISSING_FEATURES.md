# Missing Features for Standard Library Header Support

This document tracks missing C++20 features that prevent FlashCpp from compiling standard library headers. Features are listed in priority order based on their blocking impact.

**Last Updated**: 2025-12-10  
**Test Reference**: `tests/test_real_std_headers_fail.cpp`

## Summary

Standard headers like `<type_traits>` and `<utility>` currently fail to compile due to missing language features at the parser/semantic level. The preprocessor handles most standard headers correctly, but the parser encounters unsupported C++ constructs.

## Priority 1: Conversion Operators

**Status**: ❌ **BLOCKING** - Prevents `<type_traits>` from compiling  
**Test Case**: `tests/test_real_std_headers_fail.cpp` (lines 12-14)

### Problem

User-defined conversion operators (`operator T()`) fail with "Missing identifier" errors when accessing class members within the operator body.

### Example Failure

```cpp
template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
    constexpr operator T() const noexcept { return value; }
    //                                              ^^^^^
    // Error: Missing identifier 'value'
};
```

### Root Cause

The parser recognizes conversion operator syntax (code exists in `Parser.cpp` lines 1260-1286 and 3702-3742), but the symbol table lookup fails to find static members when inside a conversion operator's function body. This is a scope resolution issue specific to conversion operators.

### Required For

- `std::integral_constant` (basis of all type traits)
- `std::true_type` / `std::false_type`
- Any type trait that inherits from `integral_constant`
- Boolean conversions in template metaprogramming

### Implementation Notes

- Parser code for conversion operators exists but scope lookup is broken
- Need to ensure member function scope includes class static members
- Test with simple `integral_constant` before attempting full `<type_traits>`

---

## Priority 2: Non-Type Template Parameters with Dependent Types

**Status**: ❌ **BLOCKING** - Required for `integral_constant`  
**Test Case**: `tests/test_real_std_headers_fail.cpp` (lines 38-40)

### Problem

Templates with non-type parameters whose types depend on other template parameters fail with "Symbol not found" errors.

### Example Failure

```cpp
template<typename T, T v>  // T is a type parameter, v is a non-type parameter of type T
struct integral_constant {
    static constexpr T value = v;  // Error: Symbol 'v' not found
};
```

### Root Cause

The parser doesn't properly track non-type template parameters when their type is a dependent type (another template parameter). The symbol `v` should be recognized as a template parameter reference.

### Required For

- `std::integral_constant<T, v>` (core type trait utility)
- `std::bool_constant<b>` (alias for `integral_constant<bool, b>`)
- Compile-time constant wrappers
- Non-type template parameter deduction

### Implementation Notes

- Template parameter tracking exists for type parameters
- Need to extend to non-type parameters with dependent types
- May interact with template argument deduction

---

## Priority 3: Template Specialization Inheritance

**Status**: ❌ **BLOCKING** - Required for type trait implementations  
**Test Case**: `tests/test_real_std_headers_fail.cpp` (lines 42-44)

### Problem

Template partial specializations that inherit from other templates don't properly propagate static members from the base class.

### Example Failure

```cpp
template<typename T>
struct is_pointer : false_type {};  // Base case: not a pointer

template<typename T>
struct is_pointer<T*> : true_type {};  // Specialization: is a pointer
//                      ^^^^^^^^^^
// The static member 'value' from true_type doesn't propagate
```

### Root Cause

When a template specialization inherits from another template (e.g., `true_type` or `false_type`), the static members (like `value`) from the base class don't become available in the derived class scope.

### Required For

- All pointer type traits (`is_pointer`, `is_member_pointer`)
- Array type traits (`is_array`)
- Reference type traits (`is_lvalue_reference`, `is_rvalue_reference`)
- Const/volatile traits (`is_const`, `is_volatile`)
- Most type traits in `<type_traits>`

### Implementation Notes

- Template inheritance exists
- Static member inheritance may need special handling
- Check `StructDeclarationNode` and member lookup in template contexts

---

## Priority 4: Reference Members in Structs

**Status**: ❌ **BLOCKING** - Required for `std::reference_wrapper` and `std::tuple`  
**Test Case**: `tests/test_real_std_headers_fail.cpp` (lines 56-59)

### Problem

Structs/classes with reference-type members crash with "Reference member initializer must be an lvalue" errors.

### Example Failure

```cpp
template<typename T>
struct reference_wrapper {
    T& ref;  // Error: Reference member initializer must be an lvalue
    
    reference_wrapper(T& x) : ref(x) {}
};
```

### Root Cause

The parser/semantic analyzer doesn't properly handle reference-type members in aggregate types. Reference members are valid C++ but require special initialization rules.

### Required For

- `std::reference_wrapper<T>`
- `std::tuple` with reference types
- `std::pair` with reference types
- Perfect forwarding utilities
- Range adaptors that hold references

### Implementation Notes

- Reference members must be initialized in constructor initializer list
- Cannot be default-initialized
- Cannot be reassigned (no copy assignment)
- May need special handling in `StructDeclarationNode`

---

## Priority 5: Compiler Intrinsics

**Status**: ⚠️ **RECOMMENDED** - Performance and standard conformance  
**Test Case**: `tests/test_real_std_headers_fail.cpp` (lines 51-54)

### Problem

Standard library implementations rely on compiler intrinsics for efficient type trait implementations and built-in operations.

### Required Intrinsics

| Intrinsic | Purpose | Fallback |
|-----------|---------|----------|
| `__is_same(T, U)` | Type equality check | Template specialization |
| `__is_base_of(Base, Derived)` | Inheritance check | Template trickery |
| `__is_pod(T)` | POD type check | Conservative assumptions |
| `__is_trivial(T)` | Trivial type check | Conservative assumptions |
| `__is_trivially_copyable(T)` | Trivially copyable check | Conservative assumptions |
| `__builtin_abs`, `__builtin_labs`, etc. | Math operations | Regular function calls |

### Required For

- Efficient type trait implementations
- Standard library math functions
- Optimized container operations
- `<type_traits>` performance

### Implementation Notes

- Not strictly required (library can use workarounds)
- Greatly improves compilation speed for type traits
- Start with `__is_same` and `__is_base_of` as most critical
- Can implement as special parser constructs or built-in functions

---

## Priority 6: Complex Preprocessor Expressions

**Status**: ⚠️ **NON-BLOCKING** - Causes warnings but doesn't fail compilation  
**Test Case**: `tests/test_real_std_headers_fail.cpp` (lines 46-49)

### Problem

Standard headers contain complex preprocessor conditionals that sometimes trigger "Division by zero in preprocessor expression" warnings.

### Example

```cpp
#if defined(__GNUC__) && (__GNUC__ * 100 + __GNUC_MINOR__) >= 409
```

When `__GNUC__` is undefined, the expression may evaluate incorrectly.

### Current Status

- Preprocessor mostly works
- Warnings appear but don't block compilation
- Most headers parse successfully despite warnings

### Required For

- Clean compilation without warnings
- Proper platform/compiler detection
- Feature detection based on compiler version

### Implementation Notes

- Lower priority than parser features
- May need better undefined macro handling in `evaluate_expression()`
- See `src/FileReader.h` preprocessor code

---

## Priority 7: Advanced Template Features

**Status**: ⚠️ **PARTIAL SUPPORT** - Some features work, others don't  
**Test Case**: `tests/test_real_std_headers_fail.cpp` (lines 61-65)

### Partially Supported

- ✅ Variadic templates (basic support exists)
- ✅ Template template parameters (partial support)
- ⚠️ Perfect forwarding (`std::forward` pattern needs testing)
- ❌ SFINAE (Substitution Failure Is Not An Error)

### SFINAE Example

```cpp
template<typename T>
typename T::value_type func(T t);  // Only valid if T has value_type

template<typename T>
int func(T t);  // Fallback if first template fails
```

### Required For

- `std::enable_if` and conditional compilation
- Concept emulation in C++17 style
- Overload resolution with templates
- Generic library utilities

### Implementation Notes

- Low priority for basic standard header support
- Needed for advanced metaprogramming
- SFINAE requires sophisticated template instantiation logic

---

## Testing Strategy

### Incremental Testing Approach

1. **Phase 1**: Fix conversion operators
   - Test: Simple `integral_constant` with conversion operator
   - Verify: Can access static members in operator body
   
2. **Phase 2**: Fix non-type template parameters
   - Test: `integral_constant<int, 42>` compiles
   - Verify: Template parameter `v` is recognized
   
3. **Phase 3**: Fix template specialization inheritance
   - Test: `is_pointer<int*>` inherits from `true_type`
   - Verify: Can access `value` member through inheritance
   
4. **Phase 4**: Add reference member support
   - Test: `reference_wrapper<int>` compiles
   - Verify: Can construct with lvalue reference
   
5. **Phase 5**: Add basic intrinsics
   - Test: `__is_same(int, int)` works in constexpr context
   - Verify: Returns `true` for same types

### Test Files

- `/tmp/test_conversion_op.cpp` - Conversion operator test
- `/tmp/test_cstddef.cpp` - Basic `<cstddef>` inclusion
- `/tmp/test_type_traits.cpp` - Full `<type_traits>` test
- `tests/test_real_std_headers_fail.cpp` - Comprehensive failure analysis

---

## References

- **Test File**: `tests/test_real_std_headers_fail.cpp` - Detailed failure analysis with all standard header attempts
- **Preprocessor Documentation**: `docs/STANDARD_HEADERS_REPORT.md` - Preprocessor and macro fixes already applied
- **Parser Code**: `src/Parser.cpp` - Main parsing logic
  - Lines 1260-1286: Conversion operator parsing (first location)
  - Lines 3702-3742: Conversion operator parsing (member function context)
- **Template Registry**: `src/TemplateRegistry.h` - Template instantiation tracking

---

## Progress Tracking

### Completed ✅

- Basic preprocessor support for standard headers
- GCC/Clang builtin type macros (`__SIZE_TYPE__`, etc.)
- Preprocessor arithmetic and bitwise operators
- `__attribute__` and `noexcept` parsing

### In Progress 🔄

- None currently

### Blocked ❌

- `<type_traits>` - Needs Priority 1, 2, 3
- `<utility>` - Needs Priority 1, 2, 3, 4
- `<vector>` - Needs all priorities 1-4
- `<algorithm>` - Needs all priorities 1-4

---

## How to Update This Document

When working on any missing feature:

1. Update the **Status** field (❌ BLOCKING → 🔄 IN PROGRESS → ✅ FIXED)
2. Add implementation notes as you discover details
3. Update the **Progress Tracking** section
4. Cross-reference with related test files
5. Document any new test cases created

When adding a new missing feature:

1. Add it in the appropriate priority section
2. Include example failure code
3. Explain root cause if known
4. List what standard library features depend on it
5. Add to **Blocked** section if it blocks a standard header
