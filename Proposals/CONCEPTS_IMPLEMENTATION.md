# C++20 Concepts Implementation Guide

## Overview
This document describes the C++20 concepts implementation in FlashCpp, including what's supported, what's planned, and what's explicitly not supported (C++23 features).

## C++20 Concepts - Currently Supported

### 1. Basic Concept Declarations
**Syntax:** `concept Name = constraint_expression;`

```cpp
concept AlwaysTrue = true;
concept AlwaysFalse = false;
```

**Status:** ✅ Fully implemented
- Parser recognizes `concept` keyword
- Creates `ConceptDeclarationNode` AST node
- Stores in global `ConceptRegistry`

### 2. Template-based Concepts
**Syntax:** `template<typename T> concept Name = constraint_expression;`

```cpp
template<typename T>
concept Integral = std::is_integral_v<T>;

template<typename T>
concept Signed = std::is_signed_v<T>;

template<typename T, typename U>
concept SameAs = std::is_same_v<T, U>;
```

**Status:** ✅ Fully implemented
- Template parameters properly parsed
- Concept stored with template parameter information
- Multiple template parameters supported

## C++20 Concepts - Planned Implementation

### 3. Requires Clauses (Leading)
**Syntax:** `template<typename T> requires constraint function(...)`

```cpp
template<typename T>
requires Integral<T>
T add(T a, T b) {
    return a + b;
}
```

**Status:** ⏳ Parser infrastructure ready, implementation pending
- AST node: `RequiresClauseNode` created but not yet used
- Needs parsing after template parameters
- Constraint evaluation not yet implemented

### 4. Requires Clauses (Trailing)
**Syntax:** `template<typename T> return_type function(...) requires constraint`

```cpp
template<typename T>
T multiply(T a, T b) requires Integral<T> {
    return a * b;
}
```

**Status:** ⏳ Planned
- Trailing syntax after function signature
- Lower priority than leading requires clauses

### 5. Requires Expressions
**Syntax:** `requires { requirement; ... }`

```cpp
template<typename T>
concept Addable = requires(T a, T b) {
    a + b;  // Expression requirement
    { a + b } -> std::same_as<T>;  // Type requirement
};
```

**Status:** ⏳ Planned
- AST node: `RequiresExpressionNode` created
- Parser needs implementation
- Constraint checking needs semantic analysis

### 6. Abbreviated Function Templates
**Syntax:** `auto function(ConceptName auto param)`

```cpp
auto add(Integral auto a, Integral auto b) {
    return a + b;
}
```

**Status:** ⏳ Planned
- Syntactic sugar for template parameters with concepts
- Lower priority feature

### 7. Constrained Template Parameters
**Syntax:** `template<ConceptName T>`

```cpp
template<Integral T>
T increment(T value) {
    return value + 1;
}
```

**Status:** ⏳ Planned
- Alternative to requires clauses
- Needs template parameter parsing update

### 8. Concept Composition
**Logical operators:** `&&`, `||`, `!`

```cpp
template<typename T>
concept SignedIntegral = Integral<T> && Signed<T>;

template<typename T>
concept IntegralOrFloating = Integral<T> || FloatingPoint<T>;
```

**Status:** ⏳ Planned
- Requires constraint expression evaluation
- Needs semantic analysis implementation

### 9. Nested Requirements
**Syntax:** Requires expressions with multiple clauses

```cpp
template<typename T>
concept Container = requires(T c) {
    typename T::value_type;
    typename T::iterator;
    { c.begin() } -> std::same_as<typename T::iterator>;
    { c.end() } -> std::same_as<typename T::iterator>;
};
```

**Status:** ⏳ Planned
- Complex requires expressions
- Type requirements with `->` syntax
- Nested type names

## C++20 Concepts - Constraint Checking

### Compile-time Constraint Evaluation
When template instantiation occurs, constraints must be evaluated:

1. **Concept lookup**: Find concept definition in registry
2. **Argument substitution**: Replace template parameters with actual types
3. **Expression evaluation**: Evaluate constraint expression
4. **Success/failure**: Accept or reject instantiation

**Status:** ⏳ Not yet implemented
- Critical for actual constraint checking
- Requires integration with template instantiation
- Error messages need improvement

## C++23 Features - Explicitly NOT Supported

The following C++23 features will NOT be implemented to stay within C++20:

### 1. Deducing `this`
```cpp
struct X {
    void foo(this X const& self);  // C++23 only
};
```
**Reason:** C++23 feature, out of scope

### 2. Multidimensional Subscript Operator
```cpp
struct Matrix {
    int operator[](int i, int j);  // C++23 only
};
```
**Reason:** C++23 feature, out of scope

### 3. Enhanced Concepts Features
- Improved constraint diagnostics (C++23)
- Additional standard library concepts (C++23)
- Concept subsumption improvements (C++23)

**Reason:** C++23 enhancements, staying with C++20 baseline

## Implementation Architecture

### AST Nodes
- **ConceptDeclarationNode**: Stores concept name, template parameters, and constraint expression
- **RequiresClauseNode**: Represents `requires` clauses on templates
- **RequiresExpressionNode**: Represents `requires { ... }` expressions

### Global Registry
- **ConceptRegistry**: Stores all declared concepts
- **Lookup**: Heterogeneous lookup with `std::string_view`
- **Storage**: Maps concept names to AST nodes

### Parser Integration
- Concept declarations parsed at top level
- Template concepts parsed after `template<...>`
- Requires clauses need parsing in function/class template context

### Semantic Analysis (Pending)
- Constraint evaluation during template instantiation
- Type trait checking
- Concept subsumption rules
- Error reporting for constraint failures

## Testing Strategy

### Current Tests
1. **test_concept_simple.cpp**: Basic concept declarations
2. **test_concept_template.cpp**: Template-based concepts
3. **test_concept_comprehensive.cpp**: Multiple concept forms

### Planned Tests
1. **test_concept_requires.cpp**: Requires clauses (prepared, not working yet)
2. **test_concept_constraints.cpp**: Constraint checking
3. **test_concept_abbreviated.cpp**: Abbreviated function templates
4. **test_concept_composition.cpp**: Concept logical operations

## Error Messages

### Current
- Basic parsing errors for malformed concepts

### Planned
- "constraint not satisfied" messages
- Show which requirement failed
- Suggest fixes (similar to other C++ compilers)

## Performance Considerations

### Compile-time
- Concept lookup: O(1) hash map lookup
- Constraint evaluation: Depends on complexity
- Template instantiation: Same as C++20 standard

### Runtime
- No runtime overhead (concepts are compile-time only)
- Same generated code as unconstrained templates

## Migration Path

### From Unconstrained Templates
```cpp
// Before (C++17)
template<typename T>
T add(T a, T b) { return a + b; }

// After (C++20)
template<typename T>
requires Addable<T>
T add(T a, T b) { return a + b; }
```

### From SFINAE
```cpp
// Before (C++17 SFINAE)
template<typename T>
std::enable_if_t<std::is_integral_v<T>, T>
add(T a, T b) { return a + b; }

// After (C++20 Concepts)
template<Integral T>
T add(T a, T b) { return a + b; }
```

## References

- **C++20 Standard**: ISO/IEC 14882:2020, Clause 13 (Concepts)
- **cppreference**: https://en.cppreference.com/w/cpp/language/constraints
- **P0734R0**: Wording Paper for Concepts (original proposal)

## Conclusion

This implementation provides basic C++20 concepts support with a clear roadmap for full compliance. The architecture is designed to be extensible and maintainable, following the existing FlashCpp compiler patterns. C++23 features are explicitly excluded to maintain focus on C++20 standard compliance.
