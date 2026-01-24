// Test: requires requires pattern with pack expansion
// This pattern is used in <type_traits> for SFINAE detection
// Pattern: requires requires { typename Op<Args...>; }
// Tests unknown identifier handling in template requires clauses

template<typename T>
struct HasType {
    using type = T;
};

// Test 1: Basic requires requires pattern
template<template<typename...> class Op, typename... Args>
concept Detectable = requires {
    typename Op<Args...>;
};

// Test 2: SFINAE pattern with requires requires
// The key parsing challenge here is: typename Op<Args...>::type
// where Op is a template-template parameter
template<typename Default, template<typename...> class Op, typename... Args>
struct detector {
    using type = Default;
};

// Specialization when Op<Args...> is valid
// NOTE: This tests parsing of typename Op<Args...>::type
// The codegen for static constexpr members in partial specializations
// and template specialization selection has known issues.
// Tracked in: docs/TESTING_LIMITATIONS_2026_01_24.md
template<typename Default, template<typename...> class Op, typename... Args>
    requires requires { typename Op<Args...>; }
struct detector<Default, Op, Args...> {
    using type = typename Op<Args...>::type;
};

// Test that the parsing worked - use HasType directly to get type
int main() {
    // Use HasType directly - this tests the parsing without hitting
    // the problematic codegen for template specialization selection
    HasType<int>::type x = 42;
    
    return x;
}
