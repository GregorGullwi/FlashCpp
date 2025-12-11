# Missing Features for Standard Library Header Support

This document tracks missing C++20 features that prevent FlashCpp from compiling standard library headers. Features are listed in priority order based on their blocking impact.

**Last Updated**: 2025-12-10  
**Test Reference**: `tests/test_real_std_headers_fail.cpp`

## Summary

Standard headers like `<type_traits>` and `<utility>` currently fail to compile due to missing language features at the parser/semantic level. The preprocessor handles most standard headers correctly, but the parser encounters unsupported C++ constructs.

## Priority 1: Conversion Operators

**Status**: ✅ **FIXED** - Conversion operators work correctly with static member access  
**Test Case**: `tests/test_real_std_headers_fail.cpp` (lines 12-14), `/tmp/test_integral_constant.cpp`

### Problem

User-defined conversion operators (`operator T()`) failed with "Missing identifier" errors when accessing static class members within the operator body. Root cause was static member access in template classes during template body parsing.

### Example Success

```cpp
template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;  // Static member with non-type template parameter initializer
    constexpr operator T() const noexcept { return value; }  // ✅ WORKS NOW!
    //                                              ^^^^^
    // Static member 'value' is now accessible
};
```

### Solution Implemented (Approach B - Deferred Template Body Parsing)

**Architecture**: Implemented true C++ two-phase lookup by deferring template member function body parsing until instantiation.

**Implementation Phases**:
1. ✅ **Phase 1**: Skip template body parsing during definition - store positions in `DeferredTemplateMemberBody`
2. ✅ **Phase 2**: Parse deferred bodies during instantiation when TypeInfo is available
3. ✅ **Phase 3**: Function matching by name (not pointers) during instantiation
4. ✅ **Phase 4**: Fixed segfault in lexer position restore
5. ✅ **Phase 5**: Selective deferring - only templates with static members (preserves compatibility)
6. ✅ **Phase 6**: Template parameter substitution for non-type parameters in deferred bodies
7. ✅ **Phase 7**: Static member code generation via GlobalLoad IR

**Key Changes**:
- **Parser.cpp**: Deferred body parsing, template parameter substitution (both TemplateParameterReferenceNode and IdentifierNode)
- **Parser.h**: Added `template_param_substitutions_` tracking
- **CodeGen.h**: Static member access via GlobalLoad IR, pattern template skipping
- **AstNodeTypes.h**: DeferredTemplateMemberBody structure

**Test Results**:
- ✅ `integral_constant<int, 42>` compiles and runs correctly
- ✅ Static member `value` accessible in conversion operator
- ✅ Template parameter `v` substituted with `42`
- ✅ Conversion operator `operator T()` works
- ✅ Existing tests (test_conditional_true, template_class_methods, template_basic) pass

### Required For

- `std::integral_constant` (basis of all type traits)
- `std::true_type` / `std::false_type`
- Any type trait that inherits from `integral_constant`
- Boolean conversions in template metaprogramming

### Implementation Notes

1. **Phase 1: Skip Template Body Parsing During Definition** ✅ COMPLETE
   - Modified `parse_struct_declaration()` at line 4717-4746
   - When `parsing_template_class_ == true`, DON'T parse delayed_function_bodies
   - Instead, convert to DeferredTemplateMemberBody and store in pending_template_deferred_bodies_
   - Added DeferredTemplateMemberBody structure to AstNodeTypes.h (stores body positions and context)
   - Modified TemplateClassDeclarationNode to hold deferred_bodies_ vector
   - In parse_template_declaration, attach pending bodies to TemplateClassDeclarationNode
   - Template function bodies are now deferred until instantiation

2. **Phase 2: Parse Bodies During Instantiation** ✅ COMPLETE
   - Modified `try_instantiate_template_class()` to parse deferred bodies after TypeInfo creation
   - Deferred bodies are retrieved from TemplateClassDeclarationNode
   - For each body: restore position, convert to DelayedFunctionBody, parse with instantiated context
   - At this point, `struct_type_index` is valid and TypeInfo exists in gTypesByName
   - Added static member lookup in TypeInfo check (lines 11180-11195)
   - Static members are now found during deferred body parsing!
   - **Status**: Basic functionality works - static members are found
   - **Issue**: Segfault in downstream code (code generation or IR conversion)

3. **Phase 3: Fix Remaining Issues and Template Parameter Substitution** ✅ COMPLETE
   - Fixed function node pointer issues by matching functions by name instead of raw pointers
   - Modified DeferredTemplateMemberBody to store function name, const qualifier, and other metadata
   - Updated instantiation code to find matching functions in instantiated struct by name
   - Functions are correctly matched and bodies are attached
   - **Status**: Parsing phase complete, bodies are successfully parsed and attached
   - **Remaining Issue**: Segfault in downstream code (likely code generation or IR conversion)
   - Next: Investigate code generation issue

4. **Phase 4: Fix Segfault - Position Restore Issue** ✅ COMPLETE
   - Identified that segfault was caused by invalid position restore after deferred body parsing
   - Root cause: saved_pos from save_token_position() was being restored incorrectly
   - Solution: Use restore_lexer_position_only() instead of restore_token_position()
   - Added validity check before restoring position
   - **Status**: Segfault fixed, parsing completes successfully
   - **New Issue**: Code generation error - template parameter 'v' not substituted (expected, substitution not yet implemented)

5. **Phase 5: Fix Broken Tests - Selective Deferring** ✅ COMPLETE
   - **Problem**: Deferring ALL template bodies broke templates that use template parameters in function bodies
   - **Root Cause**: Template parameters not available during deferred parsing
   - **Solution**: Only defer bodies for templates that have static members
   - Check for static members before deferring (`has_static_members` flag)
   - Templates without static members parse bodies normally (preserves template parameter access)
   - Templates with static members defer bodies (enables static member access)
   - **Status**: Tests without static members now work correctly
   - **Remaining**: Template parameter substitution needed for deferred bodies (Phase 6)

6. **Phase 6: Template Parameter Substitution in Deferred Bodies** ✅ COMPLETE
   - **Parsing-Level Implementation**: Fully implemented template parameter substitution during deferred body parsing
   - Added `template_param_substitutions_` map in Parser to track parameter values
   - Non-type template parameters (like `T v` in `template<typename T, T v>`) are now substituted with actual values
   - Modified identifier lookup to check substitutions and return NumericLiteralNode instead of TemplateParameterReferenceNode
   - Also added substitution in static member initializers during template instantiation
   - **Test Results**: 
     - ✅ Template bodies parse successfully with template parameter references
     - ✅ `integral_constant<int, 42>` parses correctly - `v` substituted with `42` in function bodies
     - ✅ Static member access (`value`) works in deferred bodies
   - **Remaining Issue**: Code generation error for static members (out of scope for parsing phase)
   - The issue is in CodeGen, not in parsing - static members exist in StructTypeInfo but code generator can't find them

7. **Phase 7: Code Generation for Static Members** (NEXT - OUT OF SCOPE FOR THIS PR)
   - Parsing phase is complete and working
   - Issue: Code generator looks for symbols in symbol table, but static members are stored in StructTypeInfo
   - Error: "Symbol 'x' not found in symbol table during code generation"
   - This requires changes to CodeGen.h/IR generation, not Parser
   - Recommendation: Address in separate PR focused on code generation

**Key Code Locations**:
- Template class parsing: `parse_struct_declaration()` lines 4715-4746
- Template instantiation: `try_instantiate_template_class()` around line 19707+
- Delayed body parsing: `parse_delayed_function_body()` line 7463+
- Template storage: `TemplateClassDeclarationNode` in AstNodeTypes.h lines 1937+
- Deferred body structure: `DeferredTemplateMemberBody` in AstNodeTypes.h lines 25+

**Risks and Mitigation**:
- Risk: Breaking existing template functionality
  - Mitigation: Test incrementally, keep existing non-template code unchanged
- Risk: Complex parameter substitution
  - Mitigation: Reuse existing template parameter tracking infrastructure
- Risk: Position restoration after multiple instantiations  
  - Mitigation: Use SaveHandle system already in place

**Approach C: Add Static Members to AST with Separate Storage** (High Risk)
- Modify `StructDeclarationNode` to have a separate `static_members_` vector
- Don't mix static and instance members in `members_`
- Special lookup path for static members that doesn't create `this->` transforms
- Requires very careful handling to avoid the lookup loops encountered in previous attempts
- Need to ensure IdentifierNode creation doesn't trigger recursive lookups
- **Status**: All AST-based approaches so far have failed with hangs
- **Needs**: Understanding of why node creation causes hangs

### Required For

- `std::integral_constant` (basis of all type traits)
- `std::true_type` / `std::false_type`
- Any type trait that inherits from `integral_constant`
- Boolean conversions in template metaprogramming

### Implementation Notes

- **DO NOT** start with conversion operators - fix template static member access first
- Test with simple template + static member before adding conversion operators
- Consider whether template bodies should be parsed lazily (deferred until instantiation)
- May need to distinguish between template definition parsing and template instantiation contexts

### Test Progression

1. First make this work: `template<typename T> struct S { static int x; int f() { return x; } };`
2. Then add non-type params: `template<typename T, T v> struct S { static constexpr T x = v; T f() { return x; } };`
3. Then add conversion operators: `... constexpr operator T() { return x; }`
4. Finally test with `<type_traits>` header

---

## Priority 2: Non-Type Template Parameters with Dependent Types + Static Member Access

**Status**: ✅ **FIXED** - Static members in template classes now accessible via deferred body parsing  
**Test Case**: `tests/test_real_std_headers_fail.cpp` (lines 38-40), `/tmp/test_integral_constant.cpp`

### Problem

Templates with non-type parameters whose types depend on other template parameters failed when static members were referenced in member function bodies. Static member access in template class definitions didn't work during template body parsing.

### Example Success

```cpp
template<typename T, T v>  // T is a type parameter, v is a non-type parameter of type T
struct integral_constant {
    static constexpr T value = v;  // ✅ v recognized and substituted during instantiation
    
    int get() { return value; }     // ✅ WORKS NOW! Static member accessible
    //                  ^^^^^        // Static member found during deferred body parsing
};

// Usage:
integral_constant<int, 42> ic;
int result = ic.get();  // Returns 42
```

### Solution Implemented

Same solution as Priority 1 (Approach B - Deferred Template Body Parsing). See Priority 1 for full implementation details.

**Key Points**:
- Template bodies are deferred until instantiation when TypeInfo exists
- Template parameters (like `v`) are substituted with actual values during deferred parsing
- Static members are looked up in TypeInfo during deferred body parsing
- Code generation accesses static members via GlobalLoad IR with qualified names

**Test Results**: All Priority 2 features working correctly.

### Required For

- `std::integral_constant<T, v>` (core type trait utility)
- `std::bool_constant<b>` (alias for `integral_constant<bool, b>`)
- Compile-time constant wrappers
- Non-type template parameter deduction

### Test Cases to Create

```cpp
// Test 1: Basic template static member
template<typename T>
struct Test1 {
    static int count;
    int get() { return count; }  // Should find 'count'
};

// Test 2: Template with constexpr static
template<typename T>
struct Test2 {
    static constexpr int value = 42;
    int get() { return value; }  // Should find 'value'
};

// Test 3: Non-type parameter with dependent type
template<typename T, T v>
struct Test3 {
    static constexpr T value = v;  // v lookup works
    T get() { return value; }       // value lookup fails
};
```

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
