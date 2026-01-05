# Type Trait Intrinsics Implementation Guide

This document describes how to add new type trait intrinsics to FlashCpp.

## Overview

Type trait intrinsics are compiler built-in functions that provide compile-time information about types. They are essential for implementing the C++ standard library's `<type_traits>` header.

## Recent Additions (January 2026)

Added support for:
- `__has_trivial_destructor(T)` - GCC/Clang intrinsic equivalent to `__is_trivially_destructible`
- `__has_virtual_destructor(T)` - Check if type has virtual destructor

## How to Add a New Type Trait Intrinsic

### 1. Add to TypeTraitKind Enum

**File:** `src/AstNodeTypes.h`

Add a new enum value to `TypeTraitKind`:

```cpp
enum class TypeTraitKind {
    // ... existing values ...
    YourNewTrait,    // __your_new_trait(T) - Description of what it checks
};
```

### 2. Register in Parser

**File:** `src/Parser.cpp`

Add to the `trait_map` in the type trait parsing section (around line 13233):

```cpp
static const std::unordered_map<std::string_view, TraitInfo> trait_map = {
    // ... existing entries ...
    {"__your_new_trait", {TypeTraitKind::YourNewTrait, false, false, false}},
    //                                                  ^      ^      ^
    //                                                  |      |      |
    //                              is_binary (2 types)─┘      |      |
    //                              is_variadic (T + Args...)──┘      |
    //                              is_no_arg (like __is_constant_evaluated)─┘
};
```

**Parameters:**
- `is_binary`: true if the trait takes two type arguments (e.g., `__is_base_of(Base, Derived)`)
- `is_variadic`: true if the trait takes a type plus additional arguments (e.g., `__is_constructible(T, Args...)`)
- `is_no_arg`: true if the trait takes no arguments (e.g., `__is_constant_evaluated()`)

### 3. Add to __has_builtin Support

**File:** `src/FileReader.h`

Add to the `supported_builtins` set (around line 1569):

```cpp
static const std::unordered_set<std::string_view> supported_builtins = {
    // ... existing entries ...
    "__your_new_trait",
};
```

This allows the preprocessor to evaluate `#if __has_builtin(__your_new_trait)` correctly.

### 4. Implement Evaluation Logic

**File:** `src/CodeGen.h`

Add a case statement in the `visitTypeTraitExprNode` function (around line 16000-16500):

```cpp
case TypeTraitKind::YourNewTrait:
    // __your_new_trait(T) - Description of what it checks
    
    // For scalar types (int, float, pointers, etc.)
    if (isScalarType(type, is_reference, pointer_depth)) {
        result = true;  // or false, depending on the trait
    }
    // For class types
    else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
             !is_reference && pointer_depth == 0) {
        const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
        const StructTypeInfo* struct_info = type_info.getStructInfo();
        if (struct_info) {
            // Implement your logic here
            // Example: check if class has a specific property
            result = struct_info->some_property;
        }
    }
    break;
```

**Common checks:**
- `isScalarType(type, is_reference, pointer_depth)` - Check if type is a scalar (int, float, pointer, etc.)
- `type == Type::Struct` - Check if type is a class/struct
- `struct_info->has_vtable` - Check if class has virtual functions
- `struct_info->is_union` - Check if type is a union
- `struct_info->hasUserDefinedDestructor()` - Check if class has user-defined destructor
- `struct_info->base_classes` - Access base class information

### 5. Add Tests

Create test files in the `tests/` directory:

**Example 1: Simple test (test_your_trait_ret1.cpp)**
```cpp
// Test __your_new_trait intrinsic
// Should return 1 if int satisfies the trait

int main() {
    if (__your_new_trait(int)) {
        return 1;
    }
    return 0;
}
```

**Example 2: Class test (test_your_trait_class_ret42.cpp)**
```cpp
// Test __your_new_trait with classes

struct Simple {
    int x;
};

struct Complex {
    virtual ~Complex() {}
};

int main() {
    bool simple = __your_new_trait(Simple);
    bool complex = __your_new_trait(Complex);
    
    // Return 42 if both checks are correct
    if (expected_condition) {
        return 42;
    }
    return 0;
}
```

### 6. Build and Test

```bash
# Build FlashCpp
make main CXX=clang++

# Test your new intrinsic
cd /tmp
/path/to/FlashCpp tests/test_your_trait_ret1.cpp -o test.o
clang++ -no-pie -o test test.o -lstdc++ -lc
./test
echo $?  # Should print expected return value

# Run full test suite
cd /path/to/FlashCpp
./tests/run_all_tests.sh
```

## Examples from Recent Work

### __has_trivial_destructor

A unary trait that checks if a type has a trivial destructor:

```cpp
// Enum value
HasTrivialDestructor,

// Parser registration
{"__has_trivial_destructor", {TypeTraitKind::HasTrivialDestructor, false, false, false}},

// Evaluation logic
case TypeTraitKind::HasTrivialDestructor:
    if (isScalarType(type, is_reference, pointer_depth)) {
        result = true;  // Scalars have trivial destructors
    }
    else if (type == Type::Struct && /* ... */) {
        // Class is trivially destructible if:
        // - No vtable (no virtual methods)
        // - No user-defined destructor
        result = !struct_info->has_vtable && !struct_info->hasUserDefinedDestructor();
    }
    break;
```

### __has_virtual_destructor

A unary trait that checks if a type has a virtual destructor:

```cpp
// Enum value
HasVirtualDestructor,

// Parser registration
{"__has_virtual_destructor", {TypeTraitKind::HasVirtualDestructor, false, false, false}},

// Evaluation logic
case TypeTraitKind::HasVirtualDestructor:
    if (type == Type::Struct && /* ... */) {
        if (struct_info && !struct_info->is_union) {
            // Check if destructor is virtual
            result = struct_info->has_vtable && struct_info->hasUserDefinedDestructor();
            
            // Also check base classes for virtual destructor
            if (!result && struct_info->has_vtable) {
                for (const auto& base : struct_info->base_classes) {
                    // Check each base class...
                }
            }
        }
    }
    break;
```

## Binary Traits

For traits that take two type arguments (e.g., `__is_base_of(Base, Derived)`):

```cpp
// Parser registration (note is_binary = true)
{"__is_base_of", {TypeTraitKind::IsBaseOf, true, false, false}},

// Evaluation logic
case TypeTraitKind::IsBaseOf:
    if (traitNode.has_second_type()) {
        const ASTNode& second_node = traitNode.second_type_node();
        if (second_node.is<TypeSpecifierNode>()) {
            const TypeSpecifierNode& second_spec = second_node.as<TypeSpecifierNode>();
            // Compare first type (base) with second type (derived)
            // ...
        }
    }
    break;
```

## Variadic Traits

For traits that take a type plus additional arguments (e.g., `__is_constructible(T, Args...)`):

```cpp
// Parser registration (note is_variadic = true)
{"__is_constructible", {TypeTraitKind::IsConstructible, false, true, false}},

// Evaluation logic
case TypeTraitKind::IsConstructible:
    // First type is the type to construct
    // Additional types are constructor arguments
    const std::vector<ASTNode>& arg_types = traitNode.additional_type_nodes();
    for (const auto& arg_type : arg_types) {
        // Process each argument type...
    }
    break;
```

## Testing Checklist

Before submitting changes:

1. ✅ Enum value added to `TypeTraitKind`
2. ✅ Registered in Parser's `trait_map`
3. ✅ Added to FileReader's `supported_builtins`
4. ✅ Evaluation logic implemented in CodeGen
5. ✅ Test cases created and pass
6. ✅ Full test suite passes (830/830 tests)
7. ✅ Changes documented

## Common Pitfalls

1. **Forgetting to add to __has_builtin**: Standard library headers often check `#if __has_builtin(...)` before using intrinsics. Don't forget to add to `supported_builtins` in FileReader.h.

2. **Incorrect is_binary/is_variadic flags**: Make sure the flags match the number and type of arguments the trait expects.

3. **Not handling all type categories**: Consider scalar types, class types, unions, references, pointers, and arrays.

4. **Not checking base classes**: Some traits (like `__has_virtual_destructor`) need to check inherited properties from base classes.

## References

- Type trait intrinsics list: https://gcc.gnu.org/onlinedocs/gcc/Type-Traits.html
- C++ standard library type_traits: https://en.cppreference.com/w/cpp/header/type_traits
- STANDARD_HEADERS_MISSING_FEATURES.md - Current status of standard header support

## See Also

- Implementation commit: c1d9ecc (January 2026)
- Test files: `tests/test_has_trivial_destructor_ret1.cpp`, `tests/test_has_virtual_destructor_ret42.cpp`
