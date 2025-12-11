# Missing Features for Standard Library Header Support

This document tracks missing C++20 features that prevent FlashCpp from compiling standard library headers. Features are listed in priority order based on their blocking impact.

**Last Updated**: 2025-12-11  
**Test Reference**: `tests/test_real_std_headers_fail.cpp`

## Summary

Standard headers like `<type_traits>` and `<utility>` are becoming increasingly viable as key language features have been implemented. The preprocessor handles most standard headers correctly, and most critical parser/semantic features are now complete:

- ✅ Conversion operators with static member access
- ✅ Non-type template parameters with dependent types
- ✅ Template specialization inheritance
- ✅ Reference members in structs
- ⚠️ Compiler intrinsics (most critical ones implemented, including __is_same)
- ✅ Anonymous template parameters
- ✅ Type alias access from template specializations (NEW!)

The main remaining gaps are advanced template features (SFINAE, complex template metaprogramming) and some edge cases in existing features.

## Completed Features ✅

### Core Template Features
1. **Conversion Operators** - User-defined conversion operators (`operator T()`) with static member access
2. **Non-Type Template Parameters** - `template<typename T, T v>` patterns with dependent types
3. **Template Specialization Inheritance** - Static members propagate through base classes
4. **Anonymous Template Parameters** - `template<bool, typename T>` and `template<typename, class>` syntax
5. **Type Alias Access from Specializations** - Accessing `using` type aliases from template specializations
   - Test: `tests/test_type_alias_from_specialization.cpp`
   - Example: `template<> struct enable_if<true> { using type = int; }; enable_if<true>::type x;`
   - Critical for `<type_traits>` patterns like `enable_if`, `conditional`, etc.

### Language Features
6. **Reference Members in Structs** - Reference-type members in classes/structs
   - Supports: `int&`, `char&`, `short&`, `struct&`, template wrappers
   - Known limitation: `double&` has runtime issues (pre-existing bug)

All completed features maintain backward compatibility - all 627 existing tests continue to pass.

---

## Priority 5: Compiler Intrinsics (PARTIALLY IMPLEMENTED)

**Status**: ⚠️ **PARTIAL** - Many intrinsics already work, __is_same newly added  
**Test Case**: `tests/test_type_traits_intrinsics.cpp`, `tests/test_is_same_intrinsic.cpp`

### Problem

Standard library implementations rely on compiler intrinsics for efficient type trait implementations and built-in operations.

### Implemented Intrinsics ✅

| Intrinsic | Status | Notes |
|-----------|--------|-------|
| `__is_same(T, U)` | ✅ **NEW** | Type equality check - fully working |
| `__is_base_of(Base, Derived)` | ✅ WORKING | Inheritance check |
| `__is_class(T)` | ✅ WORKING | Class type check |
| `__is_enum(T)` | ✅ WORKING | Enum type check |
| `__is_union(T)` | ✅ WORKING | Union type check |
| `__is_polymorphic(T)` | ✅ WORKING | Has virtual functions |
| `__is_abstract(T)` | ✅ WORKING | Has pure virtual functions |
| `__is_final(T)` | ✅ WORKING | Class marked final |
| `__is_empty(T)` | ✅ WORKING | No non-static data members |
| `__is_standard_layout(T)` | ✅ WORKING | Standard layout type |
| `__is_trivially_copyable(T)` | ✅ WORKING | Trivially copyable check |
| `__is_trivial(T)` | ✅ WORKING | Trivial type check |
| `__is_pod(T)` | ✅ WORKING | POD type check |
| `__is_void(T)` | ✅ WORKING | Void type check |
| `__is_integral(T)` | ✅ WORKING | Integral type check |
| `__is_floating_point(T)` | ✅ WORKING | Floating point type check |
| `__is_array(T)` | ✅ WORKING | Array type check |
| `__is_pointer(T)` | ✅ WORKING | Pointer type check |
| `__builtin_labs` | ✅ WORKING | Long absolute value |
| `__builtin_llabs` | ✅ WORKING | Long long absolute value |
| `__builtin_fabs` | ✅ WORKING | Double absolute value |
| `__builtin_fabsf` | ✅ WORKING | Float absolute value |
| `__builtin_va_start` | ✅ WORKING | Variadic argument support |
| `__builtin_va_arg` | ✅ WORKING | Variadic argument access |

### Required For

- Efficient type trait implementations
- Standard library math functions
- Optimized container operations
- `<type_traits>` performance

### Recent Changes

- **2025-12-11**: Added `__is_same(T, U)` intrinsic (one of the most critical type traits)
  - Checks exact type equality including cv-qualifiers, references, pointers
  - Test: `tests/test_is_same_intrinsic.cpp` - PASSES

### Implementation Notes

- Type trait intrinsics are parsed as special expressions in Parser.cpp (lines 10217-10400)
- Evaluation logic in CodeGen.h::generateTypeTraitIr() (lines 10215+)
- Most critical intrinsics for `<type_traits>` are now implemented
- Math builtin functions (`__builtin_labs`, etc.) are registered as built-in functions

---

## Priority 6: Anonymous Template Parameters (COMPLETE)

**Status**: ✅ **COMPLETE** - Anonymous template parameters fully supported  
**Test Case**: `tests/test_anonymous_template_params.cpp`

### Problem

Standard library headers like `<type_traits>` use anonymous template parameters where the parameter has no name, only a type:

```cpp
template<bool, typename T = void>
struct enable_if { };

template<typename _Tp, typename>
struct pair_first { _Tp value; };
```

This is valid C++20 syntax but was not previously supported by the parser.

### Solution

The parser now accepts template parameters without names for both:
- **Type parameters**: `template<typename, class>`
- **Non-type parameters**: `template<bool, int>`

When a parameter name is omitted, the parser automatically generates a unique internal name (e.g., `__anon_param_0`, `__anon_type_1`) that is used for internal tracking but not exposed to user code.

### Implementation Details

- Modified `parse_template_parameter()` in Parser.cpp (lines 17657-17690 and 17722-17756)
- Checks if the next token after the type is `,`, `>`, or `=` to detect anonymous parameters
- Generates unique names using static counters for both type and non-type parameters
- Template instantiation and specialization work correctly with anonymous parameters

### Test Results

- ✅ Anonymous non-type parameters: `template<bool, typename T>` - PASSES
- ✅ Anonymous type parameters: `template<typename T, typename>` - PASSES
- ✅ Multiple anonymous parameters: `template<bool, bool, typename T>` - PASSES
- ✅ Mixed named and anonymous: `template<typename T, bool, typename>` - PASSES
- ✅ Template instantiation with anonymous parameters: `Foo<true, int>` - PASSES
- ✅ All 626 existing tests continue to pass

### Required For

- `<type_traits>` - Uses `template<bool, typename T = void> struct enable_if`
- `<utility>` - Includes `<type_traits>` which needs this feature
- Many other standard library headers that depend on `<type_traits>`

---

## Priority 7: Type Alias Access from Template Specializations (COMPLETE)

**Status**: ✅ **COMPLETE** - Type aliases from template specializations fully accessible  
**Test Case**: `tests/test_type_alias_from_specialization.cpp`

### Problem

Standard library headers like `<type_traits>` heavily use type aliases (`using type = ...`) in template specializations. Previously, accessing these aliases with qualified names (e.g., `enable_if<true>::type`) would fail during parsing.

```cpp
template<bool B> struct enable_if { using type = void; };
template<> struct enable_if<true> { using type = int; };

// This previously failed to compile:
enable_if<true>::type x = 42;
```

### Solution

The parser now properly registers type aliases from template specializations with their qualified names during template instantiation. When a specialization is instantiated (e.g., `enable_if_1` for `enable_if<true>`), all type aliases are registered globally with qualified names (e.g., `enable_if_1::type`).

### Implementation Details

- Modified `parse_member_type_alias` call in Parser.cpp (lines 15514-15527) to pass `struct_ref` instead of `nullptr`, ensuring type aliases are stored in the `StructDeclarationNode`
- Added type alias registration logic in `instantiate_full_specialization` (lines 19168-19220)
- Type aliases are registered with qualified names: `instantiated_name::alias_name`
- Handles the case where a specialization is already instantiated but type aliases haven't been registered yet

### Test Results

- ✅ Type alias from full specialization: `enable_if<true>::type x;` - PASSES
- ✅ Multiple specializations: `enable_if<true>` vs `enable_if<false>` - PASSES
- ✅ Different types in specializations: `type_identity<bool>::type` - PASSES
- ✅ Value-dependent specializations: `int_wrapper<0>::value_type` - PASSES
- ✅ All 627 existing tests continue to pass

### Required For

- `<type_traits>` - Critical for `enable_if`, `conditional`, `remove_const`, etc.
- `std::enable_if_t` - Type alias template that depends on this feature
- Template metaprogramming patterns using type traits
- SFINAE-like conditional compilation using `enable_if`

---

## Priority 8: Complex Preprocessor Expressions

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

## Priority 9: Advanced Template Features

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

1. ✅ **Phase 1**: Fix conversion operators - COMPLETE
   - Test: Simple `integral_constant` with conversion operator
   - Verify: Can access static members in operator body
   
2. ✅ **Phase 2**: Fix non-type template parameters - COMPLETE
   - Test: `integral_constant<int, 42>` compiles
   - Verify: Template parameter `v` is recognized
   
3. ✅ **Phase 3**: Fix template specialization inheritance - COMPLETE
   - Test: `is_pointer<int*>` inherits from `true_type`
   - Verify: Can access `value` member through inheritance
   
4. ✅ **Phase 4**: Add reference member support - COMPLETE
   - Test: `RefWrapper<int>` compiles and works
   - Verify: Can construct with lvalue reference and modify through reference
   
5. ✅ **Phase 5**: Add compiler intrinsics - PARTIALLY COMPLETE
   - Test: `__is_same(int, int)` works in constexpr context
   - Most critical intrinsics implemented and working

6. ✅ **Phase 6**: Anonymous template parameters - COMPLETE
   - Test: `template<bool, typename T>` compiles
   - Verify: Both type and non-type anonymous parameters work

7. ✅ **Phase 7**: Type alias access from template specializations - COMPLETE
   - Test: `enable_if<true>::type` accesses type alias from specialization
   - Verify: Type aliases properly registered with qualified names

### Test Files

- `tests/test_type_alias_from_specialization.cpp` - Type alias access from specializations (PASSES)
- `tests/test_anonymous_template_params.cpp` - Anonymous template parameters (PASSES)
- `tests/test_is_same_intrinsic.cpp` - __is_same type trait intrinsic (PASSES)
- `tests/test_type_traits_intrinsics.cpp` - Comprehensive type traits test
- `tests/test_struct_ref_members.cpp` - Reference member support (PASSES)
- `tests/test_struct_ref_member_simple.cpp` - Simple reference member test (PASSES)
- `tests/test_real_std_headers_fail.cpp` - Comprehensive failure analysis
- `/tmp/test_integral_constant.cpp` - Conversion operator test (PASSES)
- `/tmp/test_simple_static_inheritance.cpp` - Static member inheritance (PASSES)

---

## References

- **Test File**: `tests/test_real_std_headers_fail.cpp` - Detailed failure analysis with all standard header attempts
- **Preprocessor Documentation**: `docs/STANDARD_HEADERS_REPORT.md` - Preprocessor and macro fixes already applied
- **Parser Code**: `src/Parser.cpp` - Main parsing logic
  - Lines 1260-1286: Conversion operator parsing (first location)
  - Lines 3702-3742: Conversion operator parsing (member function context)
  - Lines 10217-10400: Type trait intrinsic parsing
  - Lines 15514-15527: Type alias parsing in template specializations
  - Lines 17657-17690: Anonymous type parameter parsing (typename/class)
  - Lines 17722-17756: Anonymous non-type parameter parsing
  - Lines 19168-19220: Type alias registration in `instantiate_full_specialization`
- **Code Generator**: `src/CodeGen.h` - Code generation
  - Lines 10215+: Type trait evaluation logic (generateTypeTraitIr)
- **Template Registry**: `src/TemplateRegistry.h` - Template instantiation tracking

---

## Progress Tracking

### Completed ✅

- ✅ **Priority 1**: Conversion operators with static member access
- ✅ **Priority 2**: Non-type template parameters with dependent types
- ✅ **Priority 3**: Template specialization inheritance (static members propagate through base classes)
- ✅ **Priority 4**: Reference members in structs (int&, char&, short&, struct&, template wrappers)
- ⚠️ **Priority 5**: Compiler intrinsics (partially complete - most critical intrinsics work, __is_same added)
- ✅ **Priority 6**: Anonymous template parameters (both type and non-type parameters)
- ✅ **Priority 7**: Type alias access from template specializations (both full and partial specializations)
- Basic preprocessor support for standard headers
- GCC/Clang builtin type macros (`__SIZE_TYPE__`, etc.)
- Preprocessor arithmetic and bitwise operators
- `__attribute__` and `noexcept` parsing

### In Progress 🔄

- None currently

### Blocked ❌

- `<type_traits>` - May still have some missing intrinsics or advanced template features
- `<utility>` - Depends on `<type_traits>`
- `<vector>` - May need additional features
- `<algorithm>` - May need additional features

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
