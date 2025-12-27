# Template Parameter as Base Class - Implementation Plan

**Feature**: Support for using template parameters directly as base classes  
**Example**: `template<typename T> struct Derived : T {};`  
**Status**: Not Yet Implemented  
**Priority**: High (Real C++20 Feature)  
**Created**: 2025-12-27

## Overview

This document outlines the implementation plan for supporting template parameters as base classes in FlashCpp, a critical C++20 feature used extensively in template metaprogramming and standard library implementations.

## Feature Description

### What This Enables

```cpp
// Pattern 1: Direct template parameter as base
template<typename T>
struct wrapper : T {
    // Inherits all members from T
};

// Pattern 2: Used in std::conditional and type selection
template<bool B, typename T, typename F>
struct conditional;

template<typename T, typename F>
struct conditional<true, T, F> : T {};  // Inherit from first type

template<typename T, typename F>
struct conditional<false, T, F> : F {};  // Inherit from second type

// Pattern 3: std::conjunction and std::disjunction patterns
template<typename... Ts>
struct my_or;

template<>
struct my_or<> : false_type {};  // Base case

template<typename T>
struct my_or<T> : T {};  // Template parameter as base

template<typename T, typename... Rest>
struct my_or<T, Rest...> 
    : conditional<T::value, true_type, my_or<Rest...>>::type {};
```

### Why It's Important

1. **Standard Library Requirements**: Used in `<type_traits>` for:
   - `std::conditional`
   - `std::conjunction`
   - `std::disjunction`
   - `std::negation`
   - Various SFINAE helpers

2. **Template Metaprogramming**: Fundamental for:
   - Compile-time type selection
   - Policy-based design patterns
   - Mixin inheritance patterns
   - CRTP (Curiously Recurring Template Pattern) variations

3. **C++20 Compliance**: This is a standard C++20 feature that a real compiler must support.

## Current State Analysis

### What Works Today

1. **Type aliases as base classes**: `struct my_or<> : false_type {};` works when `false_type` is a using alias to a concrete type
2. **Concrete types as base classes**: `struct Derived : Base {};` works fine
3. **Template instantiation with concrete types**: When instantiating `wrapper<MyClass>`, the base class resolution works

### What Fails Today

1. **Template parameter lookup in base class position**:
   ```cpp
   template<typename T>
   struct wrapper : T {};  // ERROR: "Base class 'T' is not a struct/class"
   ```

2. **Error Location**: Parser.cpp, lines:
   - Line ~3379: Primary struct/class parsing
   - Line ~18131: Template specialization parsing (1st location)
   - Line ~18928: Template specialization parsing (2nd location)

3. **Root Cause**: The parser checks `base_type_info->type_ == Type::Struct` but template parameters have `type_ == Type::Template` (or are registered differently in `gTypesByName`).

### Debug Investigation Findings

From previous investigation:
- Template parameters ARE registered in `gTypesByName` when template parsing occurs
- The `current_template_param_names_` vector tracks active template parameters
- When `T` is encountered as a base class, it's looked up in `gTypesByName`
- The type check `if (base_type_info->type_ != Type::Struct)` rejects it
- Simply adding `&& base_type_info->type_ != Type::Template` is insufficient
- The actual type stored for template parameters may vary based on context

## Implementation Plan

### Phase 1: Core Parser Changes (High Priority)

#### 1.1 Extend Base Class Type Checking

**File**: `src/Parser.cpp`  
**Locations**: Lines ~3379, ~18131, ~18928

**Current Code**:
```cpp
const TypeInfo* base_type_info = base_type_it->second;
if (base_type_info->type_ != Type::Struct) {
    return ParseResult::error("Base class '" + std::string(base_class_name) + 
        "' is not a struct/class", base_name_token);
}
```

**Proposed Change**:
```cpp
const TypeInfo* base_type_info = base_type_it->second;

// Check if base class is a template parameter
bool is_template_param = false;
if (!current_template_param_names_.empty()) {
    for (const auto& param_name : current_template_param_names_) {
        if (param_name == base_class_name) {
            is_template_param = true;
            FLASH_LOG_FORMAT(Templates, Debug, 
                "Base class '{}' is a template parameter - deferring resolution", 
                base_class_name);
            break;
        }
    }
}

// Allow Type::Struct for concrete types OR template parameters
if (!is_template_param && base_type_info->type_ != Type::Struct) {
    return ParseResult::error("Base class '" + std::string(base_class_name) + 
        "' is not a struct/class", base_name_token);
}

// For template parameters, skip 'final' check and other concrete type validations
if (!is_template_param) {
    // Check if base class is final
    if (base_type_info->struct_info_ && base_type_info->struct_info_->is_final) {
        return ParseResult::error("Cannot inherit from final class '" + 
            std::string(base_class_name) + "'", base_name_token);
    }
}
```

**Key Points**:
- Check `current_template_param_names_` to identify template parameters
- Skip type validation for template parameters (deferred to instantiation)
- Skip 'final' class checks for template parameters
- Log template parameter detection for debugging

#### 1.2 Store Deferred Base Class Information

**Challenge**: Template parameters as base classes must be resolved during template instantiation, not during template declaration parsing.

**Proposed Approach**:
Add a flag to `StructTypeInfo` or `StructDeclarationNode` to mark base classes that need deferred resolution:

```cpp
struct BaseClassInfo {
    std::string_view name;
    TypeIndex type_index;
    AccessSpecifier access;
    bool is_virtual;
    bool is_deferred;  // NEW: true for template parameters
};
```

**Files to Modify**:
- `src/AstNodeTypes.h`: Add `is_deferred` flag to base class storage
- `src/Parser.cpp`: Set `is_deferred = true` when base is a template parameter

### Phase 2: Template Instantiation Changes (Critical)

#### 2.1 Resolve Template Parameter Base Classes During Instantiation

**File**: `src/Parser.cpp` (template instantiation functions)  
**Function**: `try_instantiate_class_template()` and related

**Current Behavior**: When instantiating `wrapper<MyClass>`:
1. Template arguments are substituted (`T` → `MyClass`)
2. Template body is re-parsed with substituted types
3. Base classes are resolved

**Required Change**: During step 3, recognize that base class `T` is a template parameter and substitute it with the actual type (`MyClass`).

**Implementation Sketch**:
```cpp
// During template instantiation, when processing base classes:
for (auto& base_class : template_struct->base_classes) {
    if (base_class.is_deferred) {
        // This base class is a template parameter
        // Find the actual type from template arguments
        auto actual_type = resolve_template_parameter_to_type(
            base_class.name, template_args);
        
        if (!actual_type) {
            return ParseResult::error("Cannot resolve template parameter base class");
        }
        
        // Validate the actual type is a struct
        if (actual_type->type_ != Type::Struct) {
            return ParseResult::error(
                "Template argument for base class must be a struct/class type");
        }
        
        // Update base class to use actual type
        instantiated_struct->add_base_class(
            actual_type->name, 
            actual_type->type_index, 
            base_class.access, 
            base_class.is_virtual);
    } else {
        // Regular base class, copy as-is
        instantiated_struct->add_base_class(
            base_class.name,
            base_class.type_index,
            base_class.access,
            base_class.is_virtual);
    }
}
```

#### 2.2 Type Validation at Instantiation

**Critical**: When a template is instantiated with concrete types, validate that:
1. Template argument used as base class IS a struct/class type
2. Template argument is not 'final' (if applicable)
3. Template argument is not a built-in type (int, char, etc.)

**Error Messages**: Should be clear and reference the instantiation site:
```
error: cannot instantiate 'wrapper<int>': template parameter T used as base class 
       must be a struct or class type, but int is not
       wrapper<int> w;
       ^
```

### Phase 3: Member Access and Name Lookup (Medium Priority)

#### 3.1 Dependent Base Class Member Access

**Challenge**: When a template uses a template parameter as a base class, member lookup becomes dependent:

```cpp
template<typename T>
struct wrapper : T {
    void use_base_member() {
        this->member;  // OK: dependent name lookup
        member;        // May fail: non-dependent name lookup
        T::static_member;  // OK: qualified lookup
    }
};
```

**Required**: Support `this->member` syntax for dependent base class member access.

**Files to Modify**:
- Member lookup in template bodies must check deferred base classes
- May require two-phase name lookup improvements

#### 3.2 Base Class Constructor Calls

**Challenge**: Derived class constructors must call base class constructors:

```cpp
template<typename T>
struct wrapper : T {
    wrapper(int x) : T(x) {}  // Must resolve T's constructor
};
```

**Required**: Constructor call `T(x)` must be deferred and resolved during instantiation.

### Phase 4: Advanced Cases (Lower Priority)

#### 4.1 Multiple Template Parameter Base Classes

```cpp
template<typename T, typename U>
struct multiple : T, U {};
```

**Implementation**: Should work automatically if single template parameter base works.

#### 4.2 Pack Expansion in Base Classes

```cpp
template<typename... Bases>
struct multi_inherit : Bases... {};
```

**Challenge**: Pack expansion in base class list.  
**Status**: May already work if pack expansion is supported elsewhere.

#### 4.3 Dependent Base Class with Template Arguments

```cpp
template<typename T>
struct wrapper : std::vector<T> {};  // T appears in base's template args
```

**Note**: This is different from template parameter as base. May already work.

## Implementation Phases Timeline

### Phase 1: Core Support (1-2 days)
- [ ] Modify base class type checking in 3 Parser.cpp locations
- [ ] Add template parameter detection logic
- [ ] Add `is_deferred` flag to base class storage
- [ ] Create test cases for basic pattern

**Test Cases**:
```cpp
// Test 1: Simple template parameter base
template<typename T> struct wrapper : T {};

// Test 2: Type aliases as base (should still work)
using int_constant = integral_constant<int, 42>;
template<> struct my_class : int_constant {};

// Test 3: Instantiation with concrete type
struct base { int x; };
wrapper<base> w;  // Should compile and inherit x
```

### Phase 2: Template Instantiation (2-3 days)
- [ ] Implement base class resolution during instantiation
- [ ] Add type validation for instantiated base classes
- [ ] Handle constructor inheritance patterns
- [ ] Add comprehensive test suite

**Test Cases**:
```cpp
// Test 4: Access inherited members
template<typename T> struct wrapper : T {
    int use_member() { return this->value; }
};

// Test 5: Constructor forwarding
template<typename T> struct wrapper : T {
    wrapper(int x) : T(x) {}
};

// Test 6: Error case - non-class type
wrapper<int> w;  // Should error: int is not a class type
```

### Phase 3: Advanced Features (1-2 days)
- [ ] Multiple template parameter bases
- [ ] Pack expansion in base classes (if not already working)
- [ ] Static member access from template parameter bases

**Test Cases**:
```cpp
// Test 7: Multiple bases
template<typename T, typename U> struct multi : T, U {};

// Test 8: Pack expansion
template<typename... Ts> struct all : Ts... {};

// Test 9: std::conditional pattern
template<bool B, typename T, typename F>
struct conditional<true, T, F> : T {};
```

## Testing Strategy

### Unit Tests

Create `tests/test_template_param_as_base.cpp`:

```cpp
// Comprehensive test suite for template parameter base classes
template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
};

using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

// Test 1: Basic inheritance
template<typename T>
struct wrapper : T {};

// Test 2: Specialization with template param base
template<typename... Ts>
struct my_or;

template<>
struct my_or<> : false_type {};

template<typename T>
struct my_or<T> : T {};

// Test 3: std::conditional-like pattern
template<bool B, typename T, typename F>
struct conditional;

template<typename T, typename F>
struct conditional<true, T, F> : T {};

template<typename T, typename F>
struct conditional<false, T, F> : F {};

int main() {
    // Test instantiations
    static_assert(my_or<>::value == false);
    static_assert(my_or<true_type>::value == true);
    static_assert(conditional<true, true_type, false_type>::value == true);
    static_assert(conditional<false, true_type, false_type>::value == false);
    
    return 0;
}
```

### Integration Tests

Test with actual standard library patterns:
- `std::conditional`
- `std::conjunction`
- `std::disjunction`
- Policy-based design examples

### Regression Tests

Ensure existing functionality still works:
- Normal inheritance
- Template specialization inheritance
- Type alias bases
- Virtual inheritance
- Multiple inheritance

## Error Handling

### Parse-Time Errors (Template Declaration)

**When to Error**:
- Never reject template parameter as base during template declaration
- All validation deferred to instantiation

### Instantiation-Time Errors

**Error Cases**:
1. Template argument is not a class type:
   ```
   error: in instantiation of 'wrapper<int>': template parameter T used as 
          base class must be a struct or class type
   ```

2. Template argument is a final class:
   ```
   error: in instantiation of 'wrapper<FinalClass>': cannot inherit from 
          final class 'FinalClass'
   ```

3. Circular dependency:
   ```
   error: in instantiation of 'wrapper<wrapper<T>>': circular dependency 
          in base class hierarchy
   ```

## Compatibility Considerations

### Backward Compatibility

**Critical**: This change must not break any existing code.

**Safety Checks**:
1. Only affects code with template parameters as base classes (new pattern)
2. Existing concrete base classes unchanged
3. Type alias bases unchanged
4. All existing 752 passing tests must continue to pass

### C++20 Standard Compliance

This feature is required for C++20 compliance. FlashCpp aims to be a real C++20 compiler, so this must be supported.

### Standard Library Impact

**Enables**:
- `<type_traits>` full support
- `<utility>` conditional patterns
- SFINAE helpers
- Various metaprogramming utilities

## Implementation Risks

### High Risk Areas

1. **Template Instantiation**: Complex logic, many edge cases
2. **Name Lookup**: Dependent vs non-dependent name lookup
3. **Constructor Inheritance**: Implicit constructor generation

### Mitigation Strategies

1. **Incremental Implementation**: Start with simplest cases
2. **Comprehensive Testing**: Add tests before each change
3. **Logging**: Add debug logging for template parameter detection
4. **Fallback**: Keep old behavior accessible via flag if needed

## Success Criteria

### Phase 1 Complete When:
- [x] Template parameter recognized as valid base class (no parse error)
- [x] `is_deferred` flag added to base class storage
- [x] Test case `test_template_param_base_simple.cpp` compiles

### Phase 2 Complete When:
- [ ] `wrapper<MyClass>` instantiates correctly and inherits members
- [ ] Type validation occurs at instantiation time
- [ ] Error messages are clear and helpful
- [ ] Test suite passes with 10+ test cases

### Phase 3 Complete When:
- [ ] Multiple template parameter bases work
- [ ] Pack expansion in bases works (if applicable)
- [ ] All advanced patterns from standard library work
- [ ] No regressions in existing 752 passing tests

### Final Success Criteria:
- [ ] Can compile `<type_traits>` patterns using template param bases
- [ ] All test cases pass
- [ ] Documentation complete
- [ ] No performance regression
- [ ] Full C++20 compliance for this feature

## Related Features

### Already Implemented
- Template specialization
- Template parameter packs
- Type aliases
- Member template aliases
- SFINAE
- Template instantiation

### Synergies
This feature combines with:
- Type trait intrinsics
- Conditional compilation
- Policy-based design
- Mixin patterns

## References

### C++ Standard
- C++20 Standard: [temp.derived] - Derived classes
- C++20 Standard: [temp.inst] - Template instantiation

### Similar Implementations
- GCC: Template parameter base support since GCC 4.x
- Clang: Full support in Clang 3.x+
- MSVC: Supported since Visual Studio 2015

### FlashCpp Code References
- `src/Parser.cpp`: Base class parsing (lines 3150-3400, 18000-18150, 18800-18950)
- `src/AstNodeTypes.h`: Type definitions and struct info
- `src/Parser.h`: Template instantiation declarations

## Appendix: Code Snippets

### Detect Template Parameter in Base Position

```cpp
bool is_template_parameter(std::string_view name, 
                           const std::vector<std::string_view>& template_params) {
    return std::find(template_params.begin(), template_params.end(), name) 
           != template_params.end();
}
```

### Resolve Template Parameter to Concrete Type

```cpp
const TypeInfo* resolve_template_param_type(
    std::string_view param_name,
    const std::vector<TemplateTypeArg>& template_args,
    const std::vector<std::string_view>& param_names) {
    
    // Find parameter index
    auto it = std::find(param_names.begin(), param_names.end(), param_name);
    if (it == param_names.end()) {
        return nullptr;
    }
    
    size_t param_index = std::distance(param_names.begin(), it);
    if (param_index >= template_args.size()) {
        return nullptr;
    }
    
    // Get the concrete type from template arguments
    const auto& arg = template_args[param_index];
    return arg.type_info;  // or however types are accessed
}
```

## Conclusion

Implementing template parameters as base classes is essential for C++20 compliance and standard library support in FlashCpp. While the implementation is non-trivial, the approach is well-defined:

1. **Phase 1**: Recognize template parameters as valid bases (defer type checking)
2. **Phase 2**: Resolve template parameters to concrete types during instantiation
3. **Phase 3**: Handle advanced cases and edge conditions

The estimated timeline is 4-7 days for full implementation, with incremental milestones that can be tested and validated independently.

**Next Step**: Begin Phase 1 implementation with parser modifications and basic test cases.
