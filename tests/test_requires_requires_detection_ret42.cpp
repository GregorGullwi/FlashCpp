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
template<typename Default, template<typename...> class Op, typename... Args>
struct detector {
    using type = Default;
    static constexpr bool value = false;
};

// Specialization when Op<Args...> is valid
template<typename Default, template<typename...> class Op, typename... Args>
    requires requires { typename Op<Args...>; }
struct detector<Default, Op, Args...> {
    using type = typename Op<Args...>::type;
    static constexpr bool value = true;
};

// Test 3: Use the pattern
template<typename T>
using result_t = typename detector<int, HasType, T>::type;

int main() {
    // detector should select the specialization for HasType<int>
    // which has a type member, so value = true
    constexpr bool detected = detector<int, HasType, int>::value;
    
    // result_t<int> should be int (from HasType<int>::type)
    result_t<int> x = 42;
    
    return x;
}
