# Implementation Plan: Nested Type Access in Template Specializations with Requires Clauses

**Status:** Not Implemented  
**Date:** 2026-01-24  
**Related Test:** `test_requires_requires_detection_ret42.cpp`

## Problem Description

FlashCpp fails to parse nested type access (`typename Op<Args...>::type`) inside template class specializations that have `requires` clauses. This pattern is common in the C++ standard library for SFINAE-based type detection.

### Failing Pattern

```cpp
template<typename Default, template<typename...> class Op, typename... Args>
    requires requires { typename Op<Args...>; }
struct detector<Default, Op, Args...> {
    using type = typename Op<Args...>::type;  // ← Parser error here
    static constexpr bool value = true;
};
```

### Error Message

```
error: Expected ';' after type alias
      using type = typename Op<Args...>::type;
                                         ^
```

### Working in Clang/GCC

This pattern compiles and works correctly in clang++ and g++ with `-std=c++20`.

## Root Cause Analysis

The parser appears to have issues with:
1. Parsing nested type access (`::type`) on template template parameter instantiations (`Op<Args...>`) 
2. When inside a requires-constrained template specialization
3. The `typename` keyword before `Op<Args...>::type` may not be properly handled in this context

## Implementation Requirements

### 1. Parser Changes Needed

**File:** `src/Parser.cpp`

The parser needs to handle the following sequence when parsing type aliases inside constrained specializations:

```
'typename' template-template-param '<' pack-expansion '>' '::' nested-type-name
```

### 2. Specific Issues to Address

1. **Template Template Parameter Resolution**
   - Parser must recognize `Op<Args...>` as a valid type when `Op` is a template template parameter
   - Must handle pack expansion `Args...` in this context

2. **Nested Type Access Parsing**
   - After parsing `Op<Args...>`, parser must continue to handle `::type`
   - The `typename` keyword must be properly recognized as a disambiguation keyword

3. **Context-Sensitive Parsing**
   - This only fails inside requires-constrained specializations
   - May be related to how the parser maintains state during constraint parsing

### 3. Related Working Patterns

These similar patterns already work in FlashCpp:

```cpp
// Simple nested type access (no requires clause)
template<typename T>
using simple = typename HasType<T>::type;

// Template template parameter without nested access
template<template<typename...> class Op, typename... Args>
struct basic {
    using result = Op<Args...>;  // Works
};

// Nested type with regular template parameter (not template template)
template<typename T>
struct nested {
    using type = typename T::type;  // Works
};
```

## Testing Strategy

### Test Case: `test_requires_requires_detection_ret42.cpp`

- **Purpose:** Verify SFINAE-based type detection with requires clauses
- **Pattern:** Standard library `detected_or` pattern from `<type_traits>`
- **Expected:** Returns 42 when compiled with FlashCpp
- **Current:** Fails to parse

### Verification Steps

1. Test parses without errors
2. Template specialization is correctly selected based on requires clause
3. Nested type access resolves correctly
4. Final program returns 42

## Implementation Steps

1. **Phase 1: Diagnose Parser State**
   - Add debug logging to track parser state when encountering `::type`
   - Identify where parser loses track of the type context
   - Check if `typename` keyword is being properly handled

2. **Phase 2: Fix Type Alias Parsing**
   - Modify type alias parsing to handle template template parameter instantiations
   - Ensure `Op<Args...>` is recognized as a complete type specifier
   - Allow continuation parsing for `::` nested type access

3. **Phase 3: Handle Requires Clause Context**
   - Ensure parser state is properly maintained across requires clauses
   - Verify template template parameter bindings are available in specialization body
   - Test that pack expansion works in this context

4. **Phase 4: Testing**
   - Run `test_requires_requires_detection_ret42.cpp`
   - Verify against existing requires tests to ensure no regression
   - Test with more complex nested type patterns

## Workarounds

Until this is implemented, users can work around by:

1. Using type traits that don't require nested type access in specializations
2. Moving nested type access outside the constrained specialization
3. Using function-based SFINAE instead of requires clauses

## Priority

**Medium-High** - This pattern is used extensively in `<type_traits>` and modern C++ metaprogramming. Required for full standard library header support.

## Related Issues

- Unknown identifier handling in template contexts (partially addressed)
- Template template parameter handling in constraints
- Pack expansion in nested contexts

## References

- C++20 Standard: [temp.names]/5 - dependent name lookup
- C++20 Standard: [temp.res]/8 - requires clauses in partial specializations
- Standard library: `<type_traits>` lines 2736-2737 (detected_or pattern)
