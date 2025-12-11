# Missing Features for Standard Library Header Support

This document tracks missing C++20 features that prevent FlashCpp from compiling standard library headers. Features are listed in priority order based on their blocking impact.

**Last Updated**: 2025-12-11  
**Test Reference**: `tests/test_real_std_headers_fail.cpp`

## Summary

Standard headers like `<type_traits>` and `<utility>` are becoming increasingly viable as key language features have been implemented. The preprocessor handles most standard headers correctly, and most critical parser/semantic features are now complete:

- ✅ Conversion operators with static member access (Priority 1)
- ✅ Non-type template parameters with dependent types (Priority 2)
- ✅ Template specialization inheritance (Priority 3)
- ✅ Reference members in structs (Priority 4)
- ⚠️ Compiler intrinsics (Priority 5 - most critical ones implemented, including __is_same)

The main remaining gaps are advanced template features (SFINAE, complex template metaprogramming) and some edge cases in existing features.

## Completed Features ✅

### Priority 1: Conversion Operators (FIXED)
**Status**: ✅ **COMPLETE** - Conversion operators work with static member access  
**Test**: `/tmp/test_integral_constant.cpp`

User-defined conversion operators (`operator T()`) now work correctly when accessing static class members. Implemented via deferred template body parsing (two-phase lookup) which parses template member function bodies during instantiation when TypeInfo is available.

**Key Changes**: Deferred template body parsing, template parameter substitution, static member access via GlobalLoad IR.

### Priority 2: Non-Type Template Parameters with Dependent Types (FIXED)
**Status**: ✅ **COMPLETE** - Static members in template classes accessible  
**Test**: `/tmp/test_integral_constant.cpp`

Templates with non-type parameters whose types depend on other template parameters (e.g., `template<typename T, T v>`) now work correctly. Template parameters are substituted with actual values during deferred parsing, and static members are accessible in member function bodies.

### Priority 3: Template Specialization Inheritance (FIXED)
**Status**: ✅ **COMPLETE** - Static members propagate through inheritance  
**Test**: `/tmp/test_simple_static_inheritance.cpp`

Template partial specializations now properly propagate static members from base classes. Example: `template<typename T> struct is_pointer<T*> : true_type {};` now correctly inherits the static `value` member from `true_type`.

**Implementation**: Added `findStaticMemberRecursive()` to search base classes for static members, updated both parser (member function context) and code generator (member access) to handle inherited static members via GlobalLoad IR with qualified names from the owning class.

**Test Results**: 
- ✅ Simple inheritance: `struct Derived : Base {}` accessing inherited `Base::value` - PASSES
- ✅ All 621 existing tests continue to pass

**Required For**: Type traits (`is_pointer`, `is_array`, `is_const`, etc.) in `<type_traits>`

### Priority 4: Reference Members in Structs (FIXED)
**Status**: ✅ **COMPLETE** - Reference members work for most types  
**Test**: `tests/test_struct_ref_members.cpp`

Structs/classes with reference-type members now work correctly. Reference members are stored as pointers internally and properly initialized through constructor initializer lists.

**Supported Types**:
- ✅ `int&`, `char&`, `short&` - All integral reference types work
- ✅ `struct&` - Struct reference members work correctly
- ✅ Template reference wrappers - `template<typename T> struct RefWrapper { T& ref; }` works

**Key Implementation Details**:
- Reference members are tracked with `is_reference` flag in `StructMember`
- Size is set to `sizeof(void*)` during struct layout calculation (lines 4168-4174 in Parser.cpp)
- Member stores for references use `MemberStoreOp` with `is_reference` flag set
- IR converter handles reference member initialization by loading pointer values

**Test Results**: 
- ✅ Simple reference members: `struct RefHolder { int& ref; };` - PASSES
- ✅ Template reference wrappers: `RefWrapper<int>` - PASSES  
- ✅ All 621 existing tests continue to pass

**Known Limitation**: Reference members with `double&` type currently have a runtime issue (separate from reference member support itself - pre-existing double handling bug).

**Required For**: `std::reference_wrapper<T>`, `std::tuple` with references, `std::pair` with references

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
   
5. **Phase 5**: Add basic intrinsics - NEXT
   - Test: `__is_same(int, int)` works in constexpr context
   - Verify: Returns `true` for same types

### Test Files

- `/tmp/test_integral_constant.cpp` - Conversion operator test (PASSES)
- `/tmp/test_simple_static_inheritance.cpp` - Static member inheritance (PASSES)
- `tests/test_struct_ref_members.cpp` - Reference member support (PASSES)
- `tests/test_struct_ref_member_simple.cpp` - Simple reference member test (PASSES)
- `tests/test_is_same_intrinsic.cpp` - __is_same type trait intrinsic (PASSES)
- `tests/test_type_traits_intrinsics.cpp` - Comprehensive type traits test
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
  - Lines 10217-10400: Type trait intrinsic parsing
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
- ⚠️ **Priority 5**: Compiler intrinsics (partially complete - most critical intrinsics work, __is_same newly added)
- Basic preprocessor support for standard headers
- GCC/Clang builtin type macros (`__SIZE_TYPE__`, etc.)
- Preprocessor arithmetic and bitwise operators
- `__attribute__` and `noexcept` parsing

### In Progress 🔄

- None currently

### Blocked ❌

- `<type_traits>` - Most intrinsics work, may need additional features
- `<utility>` - May work now with reference member support
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
