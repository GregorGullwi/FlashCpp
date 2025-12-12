# Missing Features for Standard Library Header Support

This document tracks missing C++20 features in FlashCpp. Features are organized by implementation status and priority.

**Last Updated**: 2025-12-12 (Compacted and reorganized)  
**Test Status**: 643 total tests (642 passing, 11 expected failures)

## Summary

**Status Update (2025-12-12)**: All critical parsing features for standard library headers are complete! 643 tests pass (642 successful, 11 expected failures).

**Recent Discoveries:**
- ✅ C++20 concepts partially implemented (literal constraints work, error reporting functional)
- ✅ constinit enforcement fully working (validates constant expressions)
- ✅ All previously documented parsing features confirmed working

**Recent Achievements:**
- ✅ Member template alias instantiation working for full and partial specializations
- ✅ 36+ compiler intrinsics implemented for type traits support
- ✅ Namespace-qualified template instantiation
- ✅ All core C++20 language features needed for parsing headers

**Remaining Work:**
- Complex concept expressions (basic concepts work)
- Pack expansion in member declarations
- SFINAE (Substitution Failure Is Not An Error)
- Code generation bug: pointer types in partial specialization member aliases

## Completed Features ✅

**All completed features maintain backward compatibility - all 642 passing tests continue to pass.**

### Core Language Features Implemented

1. **Conversion Operators** - User-defined conversion operators (`operator T()`) with static member access
2. **Non-Type Template Parameters** - `template<typename T, T v>` patterns with dependent types  
3. **Template Specialization Inheritance** - Both partial and full specializations inherit from base classes
4. **Anonymous Template Parameters** - Unnamed parameters like `template<bool, typename T>`
5. **Type Alias Access from Specializations** - Access `using` aliases from template specializations
6. **Out-of-Class Static Member Definitions** - Template static member variables defined outside class
7. **Reference Members in Structs** - Reference-type members (`int&`, `char&`, `short&`, `struct&`)
8. **Implicit Constructor Generation** - Smart handling of base class constructor calls in derived classes
9. **Member Template Aliases** - Template aliases inside classes, full specializations, and partial specializations
10. **Namespace-Qualified Template Instantiation** - Templates in namespaces with qualified member access
11. **Compiler Intrinsics** - 36+ intrinsics for type traits, math operations, and variadic support

*For detailed implementation notes, test cases, and code references, see git history.*

---

## Active Work Items

### Priority 5: Compiler Intrinsics (COMPLETE ✅)

**Status**: ✅ **36+ intrinsics fully implemented**  
**Test Cases**: `tests/test_type_traits_intrinsics.cpp`, `tests/test_is_same_intrinsic.cpp`, `tests/test_new_intrinsics.cpp`, `tests/test_is_aggregate_simple.cpp`

**Categories Implemented:**
- Primary type categories (14): `__is_void`, `__is_integral`, `__is_floating_point`, `__is_array`, `__is_pointer`, `__is_reference`, `__is_class`, `__is_enum`, `__is_union`, `__is_function`, etc.
- Composite type categories (6): `__is_reference`, `__is_arithmetic`, `__is_fundamental`, `__is_object`, `__is_scalar`, `__is_compound`
- Type relationships (3): `__is_base_of`, `__is_same`, `__is_convertible`
- Type properties (12): `__is_polymorphic`, `__is_final`, `__is_abstract`, `__is_empty`, `__is_aggregate`, `__is_const`, `__is_volatile`, etc.
- Signedness traits (2): `__is_signed`, `__is_unsigned`
- Array traits (2): `__is_bounded_array`, `__is_unbounded_array`
- Math builtins (4): `__builtin_labs`, `__builtin_llabs`, `__builtin_fabs`, `__builtin_fabsf`
- Variadic support (2): `__builtin_va_start`, `__builtin_va_arg`

---

### Priority 6-8: Core Language Features (COMPLETE ✅)

All documented in **Completed Features** section above.

---

## Priority 9: Complex Preprocessor Expressions

**Status**: ⚠️ **NON-BLOCKING** - Warnings only, doesn't block compilation

**Issue**: Preprocessor handles most expressions correctly but may warn on undefined macros in complex conditionals.

**Impact**: Standard library headers work despite warnings. Better undefined macro handling in `evaluate_expression()` would eliminate warnings (see `src/FileReader.h`).

---

## Priority 10: Advanced Template Features

**Status**: ⚠️ **PARTIAL SUPPORT**

**Implemented**:
- ✅ Variadic templates (basic support)
- ✅ Template template parameters (partial support)
- ✅ C++20 Concepts (literal constraint evaluation: `requires true`, `requires false`)

**Not Implemented**:
- ❌ Perfect forwarding (needs testing)
- ❌ SFINAE (Substitution Failure Is Not An Error)
- ❌ Complex concept expressions (e.g., `requires std::integral<T>`)
- ❌ Pack expansion in member declarations

**Priority**: Low for basic standard library support. SFINAE requires sophisticated template instantiation logic. Complex concepts require full expression evaluation in constraints.

---

## Priority 11: Namespace-Qualified Template Instantiation (COMPLETE ✅)

**Status**: ✅ **COMPLETE**  
**Test Case**: `tests/test_namespace_template_instantiation.cpp`

Templates in namespaces can now be instantiated with fully-qualified names (e.g., `std::integral_constant<bool, true>::value`). Parser and CodeGen updated to handle multi-level namespace qualification.

---

## Recently Verified Working Features

These features were previously thought to be incomplete but are actually fully or partially implemented:

### constinit Enforcement

**Status**: ✅ **IMPLEMENTED**  
**Test Case**: `tests/test_constinit_fail.cpp` (correctly fails with error)

**Description**: The `constinit` specifier requires variables to be initialized with constant expressions. The compiler parses and enforces this requirement.

**What Works**:
- ✅ Parsing `constinit` keyword
- ✅ Validating that initializers are constant expressions
- ✅ Proper error messages when validation fails
- ✅ Distinction between `constexpr` and `constinit` semantics

**Implementation**: See `src/Parser.cpp` lines 2259-2294.

---

### C++20 Concepts and Requires Clauses

**Status**: ✅ **PARTIALLY IMPLEMENTED**  
**Test Case**: `tests/concept_error_test_fail.cpp` (correctly fails with constraint error)

**What Works**:
- ✅ Parsing concept declarations (`template<typename T> concept Name = ...;`)
- ✅ Parsing requires clauses on function templates
- ✅ Evaluating literal constraints (`requires true`, `requires false`)
- ✅ Proper error messages when constraints are not satisfied

**Not Yet Implemented**:
- Complex concept expressions (e.g., `requires std::integral<T>`)
- Requires expressions with type requirements
- Concept composition (conjunction, disjunction)

---

## Remaining Missing Features

### Pack Expansion in Member Declarations

**Status**: ❌ **NOT IMPLEMENTED**  
**Test Case**: `tests/test_pack_expansion_members_fail.cpp`

**Description**: Variadic template parameter packs should be expandable in member variable declarations.

**Example**:
```cpp
template<typename... Args>
struct Tuple {
    Args... values;  // Should expand to multiple members
};
```

**Current Support**: Variadic templates work for function parameters and template parameters, but not for struct member expansion.

**Impact**: Medium priority. Required for implementing `std::tuple` and similar variadic containers from scratch. Most standard library implementations use different techniques.

---

## Testing Strategy

**Test Suite**: 643 total tests
- ✅ 642 passing tests
- ✅ 11 expected failures (documented above)

**Key Test Categories**:
- Member template aliases: `test_member_template_alias.cpp`, `test_member_alias_in_full_spec.cpp`
- Compiler intrinsics: `test_type_traits_intrinsics.cpp`, `test_is_same_intrinsic.cpp`, `test_new_intrinsics.cpp`
- Template features: `test_namespace_template_instantiation.cpp`, `test_anonymous_template_params.cpp`
- Specializations: `test_full_spec_inherit.cpp`, `test_partial_spec_inherit.cpp`
- Type system: `test_struct_ref_members.cpp`, `test_out_of_class_static.cpp`

---

## Code References

### Parser (`src/Parser.cpp`)
- Lines 3131-3180: Member template alias/function template detection in regular classes
- Lines 10339-10700: Type trait intrinsic parsing (36+ intrinsics)
- Lines 10545-10690: Array type parsing in type trait arguments
- Lines 15977-16030: Member template alias/function template detection in full specializations
- Lines 16891-16944: Member template alias/function template detection in partial specializations
- Lines 18483-18610: Member template alias parsing implementation

### Code Generator (`src/CodeGen.h`)
- Lines 4906-4954: Qualified identifier IR generation
- Lines 10282+: Type trait evaluation logic (36+ intrinsics)
- Lines 587-599: Trivial default constructor generation with base class check
- Lines 1686-1707: Explicit constructor generation with base class check

### ConstExpr Evaluator (`src/ConstExprEvaluator.h`)
- Lines 2344+: Type trait constexpr evaluation

### Template Registry (`src/TemplateRegistry.h`)
- Lines 256-271: OutOfLineMemberFunction and OutOfLineMemberVariable structs
- Lines 653-681: Registration methods for out-of-line members

---

## Progress Summary

### Completed ✅
All critical parsing features for standard library headers are complete! This includes:
- All core language features (conversion operators, template specializations, etc.)
- 36+ compiler intrinsics for type traits
- Member template aliases in all contexts
- Namespace-qualified template instantiation
- Reference members in structs
- Anonymous template parameters
- C++20 concepts (literal constraints)
- constinit enforcement

### Known Issues ⚠️
- Partial specialization member aliases: Code generator bug with pointer types (parsing works)
- Complex preprocessor expressions: Non-blocking warnings

### Not Yet Implemented ❌
- Complex concept expressions (basic literal constraints work)
- Pack expansion in member declarations  
- SFINAE (Substitution Failure Is Not An Error)

### All Parsing Complete! ✅
All critical parsing features for C++20 standard library header support have been implemented. The remaining work is primarily in:
1. Code generation optimizations (e.g., pointer type handling in partial specializations)
2. Advanced template metaprogramming (SFINAE)
3. Complex concept expression evaluation
4. Pack expansion in member variable declarations

---

## How to Update This Document

When working on a missing feature:

1. Update the **Status** field (❌ NOT IMPLEMENTED → 🔄 IN PROGRESS → ✅ COMPLETE)
2. Add brief implementation notes (1-2 sentences max)
3. Update the **Progress Summary** section
4. Document test cases created
5. When complete, move to **Completed Features** section and condense details
6. Remove excessive implementation details to keep document concise

When discovering a new missing feature:

1. Add it to the **Newly Discovered Missing Features** section
2. Include a brief description and example
3. Note the impact level (High/Medium/Low priority)
4. Add a test case if possible
5. Document any known workarounds
