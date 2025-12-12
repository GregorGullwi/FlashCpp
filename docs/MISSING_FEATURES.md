# Missing Features for Standard Library Header Support

This document tracks missing C++20 features that prevent FlashCpp from compiling standard library headers. Features are listed in priority order based on their blocking impact.

**Last Updated**: 2025-12-12 (13:01 UTC)  
**Test Reference**: `tests/test_real_std_headers_fail.cpp`

## Summary

**Status Update (2025-12-12 13:01 UTC)**: **GOOD PROGRESS!** Standard library support continues to improve with 36+ compiler intrinsics now fully implemented. However, testing with real `<type_traits>` header revealed that member template aliases inside full template specializations don't parse correctly.

`<type_traits>` now parses **148 lines** (previously failed much earlier) before hitting the member template alias issue in the `__conditional<false>` specialization. This is a significant milestone showing most features work correctly. The preprocessor handles standard headers well, and nearly all critical parser/semantic features are complete.

**Recent updates**:
- **2025-12-12 13:01 UTC**: Discovered member template aliases fail in full specializations - blocks `<type_traits>` at line 148.
- **2025-12-12 13:01 UTC**: Verified bool conditionals work correctly - no bool bug exists. All 639 tests pass.
- **2025-12-12 13:01 UTC**: Tested `<type_traits>` inclusion - parses 148 lines before hitting member alias issue in `__conditional<false>`.
- **2025-12-12 12:15 UTC**: Added `__is_aggregate` intrinsic for aggregate type detection. Total: 36+ intrinsics!
- **2025-12-12 11:06 UTC**: Member template aliases (Priority 8.5) work in regular classes - cherry-picked from `main` branch.

Completed features:
- ✅ Conversion operators, non-type template parameters, template specialization inheritance
- ✅ Reference members, anonymous template parameters, type alias access from specializations
- ✅ Out-of-class static member definitions, implicit constructor generation for derived classes
- ✅ Namespace-qualified template instantiation
- ⚠️ **Member template aliases** (work in regular classes, not in full specializations)
- ✅ **36+ compiler intrinsics for type traits** (including `__is_aggregate`)

**One remaining blocker for `<type_traits>`**: Member template aliases in full template specializations (Priority 8.5).

## Completed Features ✅

**All completed features maintain backward compatibility - all 639 passing tests continue to pass (640 total tests including expected failures).**

### Core Language Features (Priorities 1-8, 11)
1. **Conversion Operators** - User-defined conversion operators (`operator T()`) with static member access
2. **Non-Type Template Parameters** - `template<typename T, T v>` patterns with dependent types  
3. **Template Specialization Inheritance** - Both partial and full specializations inherit from base classes
4. **Anonymous Template Parameters** - Unnamed parameters like `template<bool, typename T>`
5. **Type Alias Access from Specializations** - Access `using` aliases from template specializations (critical for `<type_traits>`)
6. **Out-of-Class Static Member Definitions** - Template static member variables defined outside class
7. **Reference Members in Structs** - Reference-type members (`int&`, `char&`, `short&`, `struct&`)
8. **Implicit Constructor Generation** - Smart handling of base class constructor calls in derived classes
11. **Namespace-Qualified Template Instantiation** - Templates in namespaces with qualified member access (`std::Template<Args>::member`)

*For detailed implementation notes and test cases, see git history or previous versions of this document.*

---

## Priority 5: Compiler Intrinsics (MOSTLY COMPLETE)

**Status**: ✅ **MOSTLY COMPLETE** - 36+ intrinsics fully implemented and tested  
**Test Case**: `tests/test_type_traits_intrinsics.cpp`, `tests/test_is_same_intrinsic.cpp`, `tests/test_new_intrinsics.cpp`, `tests/test_is_aggregate_simple.cpp`

### Problem

Standard library implementations rely on compiler intrinsics for efficient type trait implementations and built-in operations.

### Implemented Intrinsics ✅

**Primary type categories (14 intrinsics)**:
- `__is_void(T)`, `__is_nullptr(T)`, `__is_integral(T)`, `__is_floating_point(T)`
- `__is_array(T)`, `__is_pointer(T)`, `__is_lvalue_reference(T)`, `__is_rvalue_reference(T)`
- `__is_member_object_pointer(T)`, `__is_member_function_pointer(T)`
- `__is_enum(T)`, `__is_union(T)`, `__is_class(T)`, `__is_function(T)`

**Composite type categories (6 intrinsics)**:
- `__is_reference(T)` - lvalue or rvalue reference
- `__is_arithmetic(T)` - integral or floating point
- `__is_fundamental(T)` - void, nullptr_t, or arithmetic
- `__is_object(T)` - not function, reference, or void
- `__is_scalar(T)` - arithmetic, pointer, enum, member pointer, or nullptr
- `__is_compound(T)` - not fundamental (array, function, pointer, reference, class, union, enum, member pointer)

**Type relationships (3 intrinsics)**:
- `__is_base_of(Base, Derived)` - inheritance check
- `__is_same(T, U)` - exact type equality
- `__is_convertible(From, To)` - conversion check

**Type properties (12 intrinsics - NEW!)**:
- `__is_polymorphic(T)`, `__is_final(T)`, `__is_abstract(T)`, `__is_empty(T)`
- `__is_aggregate(T)` - aggregate type check **NEW!**
- `__is_standard_layout(T)`, `__is_trivially_copyable(T)`, `__is_trivial(T)`, `__is_pod(T)`
- `__is_const(T)` - const qualifier check
- `__is_volatile(T)` - volatile qualifier check
- `__has_unique_object_representations(T)`

**Signedness traits (2 intrinsics)**:
- `__is_signed(T)` - signed integral type
- `__is_unsigned(T)` - unsigned integral type

**Array traits (2 intrinsics)**:
- `__is_bounded_array(T)` - array with known bound (e.g., int[10])
- `__is_unbounded_array(T)` - array with unknown bound (e.g., int[])

**Math builtins (4 intrinsics)**:
- `__builtin_labs`, `__builtin_llabs`, `__builtin_fabs`, `__builtin_fabsf`

**Variadic support (2 intrinsics)**:
- `__builtin_va_start`, `__builtin_va_arg`

**Total**: 36+ compiler intrinsics fully implemented

### Required For

- Efficient type trait implementations
- Standard library math functions
- Optimized container operations
- `<type_traits>` performance
- Type-based metaprogramming

### Recent Changes

- **2025-12-12 12:15 UTC**: Added `__is_aggregate(T)` intrinsic
  - Detects aggregate types: arrays and structs with no user-declared constructors, no private/protected members, no virtual functions
  - Correctly distinguishes user-declared from compiler-generated constructors
  - Arrays are always aggregates
  - Test: `tests/test_is_aggregate_simple.cpp` - PASSES
  
- **2025-12-12 09:50 UTC**: Added 13 new type trait intrinsics for better `<type_traits>` support
  - Composite categories: `__is_reference`, `__is_arithmetic`, `__is_fundamental`, `__is_object`, `__is_scalar`, `__is_compound`
  - Type conversion: `__is_convertible(From, To)` 
  - Type properties: `__is_const`, `__is_volatile`, `__is_signed`, `__is_unsigned`
  - Array traits: `__is_bounded_array`, `__is_unbounded_array`
  - Added array type parsing support in type trait arguments (e.g., `__is_bounded_array(int[10])`)
  - Test: `tests/test_new_intrinsics.cpp` - PASSES (all 13 intrinsics tested)
  
- **2025-12-11**: Added `__is_same(T, U)` intrinsic
  - Checks exact type equality including cv-qualifiers, references, pointers
  - Test: `tests/test_is_same_intrinsic.cpp` - PASSES

### Implementation Notes

- Type trait intrinsics are parsed as special expressions in Parser.cpp (lines 10339-10700)
- Array type parsing support for bounded/unbounded arrays (lines 10545-10690)
- Evaluation logic in CodeGen.h::generateTypeTraitIr() (lines 10282+)
- Constexpr evaluation in ConstExprEvaluator.h::evaluate_type_trait() (lines 2344+)
- Most critical intrinsics for `<type_traits>` are now implemented
- Math builtin functions (`__builtin_labs`, etc.) are registered as built-in functions

---

## Priority 6: Anonymous Template Parameters (COMPLETE)

**Status**: ✅ **COMPLETE**  
**Test Case**: `tests/test_anonymous_template_params.cpp`

Enables unnamed template parameters like `template<bool, typename T>` and `template<typename, class>`. Parser generates unique internal names for tracking. All 638 tests pass.

---

## Priority 7: Type Alias Access from Template Specializations (COMPLETE)

**Status**: ✅ **COMPLETE**  
**Test Case**: `tests/test_type_alias_from_specialization.cpp`

Type aliases in template specializations are now accessible with qualified names (e.g., `enable_if<true>::type`). Critical for `<type_traits>` functionality.

---

## Priority 8: Out-of-Class Static Member Definitions in Templates (COMPLETE)

**Status**: ✅ **COMPLETE**  
**Test Case**: `tests/test_out_of_class_static.cpp`

Supports `template<typename T> Type ClassName<T>::member = value;` pattern. Template parameter substitution works correctly, including constructor call initializers like `T()`.

---

## Priority 8.5: Member Template Aliases (PARTIAL ⚠️)

**Status**: ⚠️ **PARTIAL** - Works in regular classes, but NOT in template specializations  
**Test Case**: `tests/test_member_template_alias.cpp` (passes), `tests/test_member_alias_in_full_spec.cpp` (fails)

### Problem

Member template aliases like `struct S { template<typename T> using Ptr = T*; };` work in regular struct/class definitions, but fail to parse when declared inside **full template specializations**.

### Example Failure

```cpp
template<bool>
struct __conditional {
    template<typename _Tp, typename>
    using type = _Tp;  // Works fine
};

template<>
struct __conditional<false> {
    template<typename, typename _Up>  // ERROR: "Unexpected token in type specifier: 'template'"
    using type = _Up;
};
```

### Impact

This blocks `<type_traits>` at line 148 where `std::__conditional<false>` specialization defines a member template alias. The parser fails with "Unexpected token in type specifier: 'template'" when trying to parse the member template alias inside the full specialization.

### Current Status

- ✅ Member template aliases work in regular classes
- ✅ Member template aliases work in primary template classes
- ❌ Member template aliases fail in full template specializations
- ❓ Partial template specializations not yet tested
- ⚠️ **Known limitation**: Reference aliases have the same pre-existing runtime issue as direct reference members

### Required For

- `<type_traits>` - Uses member template aliases in `__conditional<false>` and other specializations
- `<utility>` - May use member template aliases in specializations
- Modern C++ codebases that follow standard library patterns

---

## Priority 9: Complex Preprocessor Expressions

**Status**: ⚠️ **NON-BLOCKING** - Warnings only, doesn't block compilation

Preprocessor handles most expressions correctly but may warn on undefined macros in complex conditionals. Needs better undefined macro handling in `evaluate_expression()` (see `src/FileReader.h`).

---

## Priority 10: Advanced Template Features

**Status**: ⚠️ **PARTIAL SUPPORT**

- ✅ Variadic templates (basic support)
- ✅ Template template parameters (partial support)
- ⚠️ Perfect forwarding (needs testing)
- ❌ SFINAE (Substitution Failure Is Not An Error)

Low priority for basic standard library support. SFINAE requires sophisticated template instantiation logic.

---

## Priority 11: Namespace-Qualified Template Instantiation (COMPLETE)

**Status**: ✅ **COMPLETE**  
**Test Case**: `tests/test_namespace_template_instantiation.cpp`

Templates in namespaces can now be instantiated with fully-qualified names (e.g., `std::integral_constant<bool, true>::value`). Parser and CodeGen both updated to handle multi-level namespace qualification.

---

## Testing Strategy

All 640 tests: 639 pass, 1 new test documents the member alias issue. Key test files:
- `test_member_alias_in_full_spec.cpp` - **NEW**: Documents member template alias bug in full specializations
- `test_namespace_template_instantiation.cpp`, `test_is_aggregate_simple.cpp`, `test_is_aggregate_with_if.cpp`
- `test_bool_conditional_bug.cpp` - Verifies bool conditionals work correctly in if statements
- `test_full_spec_inherit.cpp`, `test_partial_spec_inherit.cpp`
- `test_type_alias_from_specialization.cpp`, `test_anonymous_template_params.cpp`
- `test_is_same_intrinsic.cpp`, `test_new_intrinsics.cpp`
- `test_struct_ref_members.cpp`, `test_out_of_class_static.cpp`

---

## References

- **Test File**: `tests/test_real_std_headers_fail.cpp` - Detailed failure analysis with all standard header attempts
- **Preprocessor Documentation**: `docs/STANDARD_HEADERS_REPORT.md` - Preprocessor and macro fixes already applied
- **Parser Code**: `src/Parser.cpp` - Main parsing logic
  - Lines 1260-1286: Conversion operator parsing (first location)
  - Lines 3702-3742: Conversion operator parsing (member function context)
  - Lines 10339-10700: **Type trait intrinsic parsing with array type support (35+ intrinsics - UPDATED 2025-12-12)**
  - Lines 10545-10690: Array type parsing in type trait arguments ([N] and [])
  - Lines 10888-10930: Qualified identifier parsing with namespace-qualified templates
  - Lines 15438-15536: Full specialization base class parsing
  - Lines 15514-15527: Type alias parsing in template specializations
  - Lines 16194-16274: Partial specialization base class parsing
  - Lines 17657-17690: Anonymous type parameter parsing (typename/class)
  - Lines 17722-17756: Anonymous non-type parameter parsing
  - Lines 19168-19220: Type alias registration in `instantiate_full_specialization`
  - Lines 20875-20933: Out-of-line static member variable processing
  - Lines 21929-21964: Out-of-line static member variable parsing
- **Code Generator**: `src/CodeGen.h` - Code generation
  - Lines 4906-4954: Qualified identifier IR generation
  - Lines 10269-10280: Helper functions (isScalarType, isArithmeticType)
  - Lines 10282+: **Type trait evaluation logic (generateTypeTraitIr) - 35+ intrinsics**
  - Lines 587-599: Trivial default constructor generation with base class check
  - Lines 1686-1707: Explicit constructor generation with base class check
  - Lines 1763-1795: Implicit copy/move constructor generation with base class check
- **ConstExpr Evaluator**: `src/ConstExprEvaluator.h` - Compile-time evaluation
  - Lines 2344+: Type trait constexpr evaluation (evaluate_type_trait)
- **Template Registry**: `src/TemplateRegistry.h` - Template instantiation tracking
  - Lines 256-271: OutOfLineMemberFunction and OutOfLineMemberVariable structs
  - Lines 653-681: Registration methods for out-of-line members

---

## Progress Tracking

### Completed ✅

- ✅ **Priority 1**: Conversion operators with static member access
- ✅ **Priority 2**: Non-type template parameters with dependent types
- ✅ **Priority 3**: Template specialization inheritance (both partial and full specializations)
- ✅ **Priority 4**: Reference members in structs (int&, char&, short&, struct&, template wrappers)
- ✅ **Priority 5**: Compiler intrinsics (**35+ intrinsics fully implemented - 2025-12-12**)
- ✅ **Priority 6**: Anonymous template parameters (both type and non-type parameters)
- ✅ **Priority 7**: Type alias access from template specializations (both full and partial specializations)
- ✅ **Priority 8**: Out-of-class static member definitions in templates
- ✅ **Priority 8b**: Implicit constructor generation for derived classes
- ⚠️ **Priority 8.5**: Member template aliases (**PARTIAL** - works in regular classes, NOT in full specializations)
- ⚠️ **Priority 9**: Complex preprocessor expressions (non-blocking warnings)
- ⚠️ **Priority 10**: Advanced template features (partial support)
- ✅ **Priority 11**: **Namespace-qualified template instantiation** (**FIXED 2025-12-12**)
- Basic preprocessor support for standard headers
- GCC/Clang builtin type macros (`__SIZE_TYPE__`, etc.)
- Preprocessor arithmetic and bitwise operators
- `__attribute__` and `noexcept` parsing

### Potential Issues ⚠️

**Newly Discovered (2025-12-12 13:01 UTC)**:
- **Member Template Aliases in Full Specializations**: Member template aliases work in regular classes and primary templates, but fail to parse inside full template specializations (e.g., `template<> struct S<false> { template<typename T> using type = T; };`). This blocks `<type_traits>` at line 148. See Priority 8.5 for details.

### Critical Blockers ❌

**One blocker found for `<type_traits>`**:
- Member template aliases in full specializations (Priority 8.5) - Prevents compilation of `<type_traits>` at line 148

### Remaining Missing Features ❌

- ⚠️ **Priority 9**: Complex preprocessor expressions (non-blocking warnings)
- ⚠️ **Priority 10**: Advanced template features (SFINAE, some metaprogramming patterns)

### In Progress 🔄

**None currently in progress.**

- ✅ **Priority 11**: Namespace-qualified template instantiation (**COMPLETE!**)

### No More Blockers! ✅

All critical blocking features for standard library header support have been implemented!

- ✅ `<type_traits>` - Now works with `std::integral_constant<T, v>` syntax
- ✅ `<utility>` - Can now use `std::` qualified templates
- ✅ `<vector>` - Namespace-qualified template syntax supported
- ✅ `<algorithm>` - Ready to use with proper qualification
- ✅ All other standard library headers - Can be compiled with qualified names

---

## How to Update This Document

When working on any missing feature:

1. Update the **Status** field (❌ BLOCKING → 🔄 IN PROGRESS → ✅ FIXED)
2. Add implementation notes as you discover details
3. Update the **Progress Tracking** section
4. Cross-reference with related test files
5. Document any new test cases created
6. Move completed features to the **Completed Features** section at the top
7. Compact the completed features list by removing excessive details

When adding a new missing feature discovered during implementation:

1. Add it in the appropriate priority section
2. Include example failure code
3. Explain root cause if known
4. List what standard library features depend on it
5. Add to **Newly Discovered Missing Features** section in Progress Tracking
6. Document any workarounds available
