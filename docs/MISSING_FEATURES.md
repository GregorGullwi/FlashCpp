# Missing Features for Standard Library Header Support

This document tracks missing C++20 features that prevent FlashCpp from compiling standard library headers. Features are listed in priority order based on their blocking impact.

**Last Updated**: 2025-12-10  
**Test Reference**: `tests/test_real_std_headers_fail.cpp`

## Summary

Standard headers like `<type_traits>` and `<utility>` currently fail to compile due to missing language features at the parser/semantic level. The preprocessor handles most standard headers correctly, but the parser encounters unsupported C++ constructs.

## Priority 1: Conversion Operators

**Status**: ❌ **BLOCKED BY PRIORITY 2** - Static member access in template classes must be fixed first  
**Test Case**: `tests/test_real_std_headers_fail.cpp` (lines 12-14)

### Problem

User-defined conversion operators (`operator T()`) fail with "Missing identifier" errors when accessing static class members within the operator body. However, this is actually a symptom of a deeper issue with static member access in template classes.

### Example Failure

```cpp
template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;  // Static member with non-type template parameter initializer
    constexpr operator T() const noexcept { return value; }
    //                                              ^^^^^
    // Error: Missing identifier 'value'
};
```

### Root Cause Analysis

**Investigation Results** (2025-12-10 - Updated):

1. **Parser Support Exists**: The parser correctly handles conversion operator syntax (`operator T()`) - code exists in `Parser.cpp` lines 1260-1286 and 3702-3742.

2. **Actual Problem**: The issue is NOT with conversion operator parsing, but with **static member lookup in template class contexts**.

3. **Why It Fails**:
   - When parsing a template class definition, member function bodies are parsed before template instantiation
   - Static members are added to `StructTypeInfo::static_members` but NOT to `StructDeclarationNode::members_` in the AST
   - Member lookup code (lines 11117-11140) only checks `struct_node->members()` which doesn't include static members
   - Static members are also not registered in the global symbol table during template parsing

4. **Why Simple Structs Work**: Non-template structs can access static members because they're looked up via `StructTypeInfo` after the struct is fully defined.

5. **Why Templates Fail**: Template class bodies are parsed before instantiation, so `StructTypeInfo` doesn't exist yet. The AST-based lookup fails because static members aren't in the AST.

6. **Attempted Fixes That Failed**:
   - Adding static members to AST with `is_static` flag → causes infinite lookup loops
   - Creating `QualifiedIdentifierNode` for static access → triggers recursive lookups  
   - Setting `identifierType` without proper node creation → parsing errors
   - Global `static_member_registry_` map → causes hangs (root cause unknown, needs debugger)
   - **Approach A Implementation Attempt** (2025-12-10):
     - Added `StructTypeInfo* struct_info` to `MemberFunctionContext`
     - Passed struct_info pointer through all delayed function body creation sites
     - Updated identifier lookup to check `context.struct_info->static_members` directly
     - Created DeclarationNode for static members and set identifierType
     - **Result**: Still caused hangs even for simple non-template cases
     - **Issue**: Creating DeclarationNode or setting identifierType for static members triggers recursive processing somewhere in the pipeline
     - All approaches that create ANY AST node for static members during lookup cause hangs

7. **Architecture Discovery**:
   - Template bodies ARE parsed immediately during template definition (line 4722-4750)
   - Delayed parsing happens AFTER struct definition but BEFORE instantiation
   - For templates, `struct_type_index` is often 0 (not in `gTypesByName` until instantiation)
   - TypeInfo lookup at line 11175 checks `if (struct_type_index != 0)` and skips for templates
   - StructTypeInfo exists but isn't accessible via type index during template parsing

### Why This Blocks Conversion Operators

The conversion operator itself parses correctly. The failure occurs when the operator body tries to return the static member `value`. This is a general template static member issue, not specific to conversion operators.

### Correct Fix Approach

**Root Architectural Issue**: Template member functions have `struct_type_index == 0`, causing TypeInfo-based static member lookup to be skipped (line 11175).

This requires a more fundamental architectural change. Three possible approaches:

**Approach A: Pass StructTypeInfo Pointer Directly** (ATTEMPTED - FAILED)
- Add `StructTypeInfo* struct_info` to `MemberFunctionContext`
- Pass struct_info pointer (not just index) when setting up member function context
- Check `struct_info->static_members` directly instead of requiring type index lookup
- Avoids the `struct_type_index == 0` problem entirely
- **Status**: FAILED - causes hangs even for simple cases
- **Problem**: Creating any AST node (DeclarationNode, IdentifierNode) for static members during lookup triggers recursive processing
- **Needs**: Debugger to trace where the hang occurs; likely in delayed parsing or code generation

**Approach B: Defer Template Body Parsing** (RECOMMENDED - Not Yet Attempted)
- Don't parse template member function bodies during template definition
- Parse them only during instantiation (true two-phase lookup)
- At instantiation time, full TypeInfo exists in `gTypesByName` and lookups work
- Major architectural change but aligns with C++ semantics
- Would require significant refactoring of template handling
- **Advantage**: Clean separation between definition and instantiation, matches C++ standard
- **Disadvantage**: Requires major refactoring of current template architecture

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

**Status**: ❌ **BLOCKING** - Required for `integral_constant` and blocks Priority 1  
**Test Case**: `tests/test_real_std_headers_fail.cpp` (lines 38-40)

### Problem

Templates with non-type parameters whose types depend on other template parameters fail. Additionally, static member access in template class definitions doesn't work during template body parsing.

### Example Failure

```cpp
template<typename T, T v>  // T is a type parameter, v is a non-type parameter of type T
struct integral_constant {
    static constexpr T value = v;  // Compiles: v is recognized as template parameter
    
    int get() { return value; }     // Error: Missing identifier 'value'
    //                  ^^^^^        // Static member not found during template parsing
};
```

### Root Cause

**Two Related Issues**:

1. **Non-Type Template Parameters**: The symbol `v` is being looked up correctly as a template parameter (logs show "SymbolTable lookup found template parameter 'v'"), so this part actually WORKS.

2. **Static Member Access** (THE REAL BLOCKER): Static members declared in template classes cannot be accessed within member function bodies during template parsing because:
   - Static members are stored in `StructTypeInfo::static_members` (runtime type system)
   - They are NOT added to `StructDeclarationNode::members_` (AST)
   - Member function body parsing happens during template definition (before instantiation)
   - At this point, `StructTypeInfo` doesn't exist yet (created during instantiation)
   - AST-based lookup fails because static members aren't in the members list

###  Investigation Results

Tested variations:
- ✅ Regular struct with static member: WORKS (uses TypeInfo lookup)
- ❌ Template struct with static member: FAILS (TypeInfo doesn't exist during parsing)
- ❌ Template struct with static member in conversion operator: FAILS (same issue)

The parser correctly identifies template parameters including non-type ones with dependent types. The failure is purely about static member visibility.

### Required For

- `std::integral_constant<T, v>` (core type trait utility)
- `std::bool_constant<b>` (alias for `integral_constant<bool, b>`)
- Compile-time constant wrappers
- Non-type template parameter deduction

### Implementation Notes

**Current Architecture**:
- Template class parsing: Immediate (builds AST, parses member bodies)
- Type info creation: Delayed (happens at instantiation)
- Static member storage: TypeInfo only (not in AST)

**Possible Solutions**:

1. **Add static members to AST** (`StructDeclarationNode::members_` with `is_static` flag)
   - Attempted but caused infinite lookup loops
   - Need to ensure static member lookup doesn't create `this->` prefix
   - Must avoid creating nodes that trigger recursive lookups

2. **Defer template body parsing** (parse bodies only at instantiation)
   - Major architectural change
   - Would allow TypeInfo to exist during member function parsing
   - Aligns with C++ two-phase lookup semantics

3. **Special template-context symbol table**
   - Register static members in a template-specific scope during parsing
   - Look up from this scope when `parsing_template_body_` is true

4. **Create TypeInfo early for templates**
   - Create StructTypeInfo during template definition (not just instantiation)
   - Populate with template parameter placeholders
   - Would need instantiation-specific copies

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
