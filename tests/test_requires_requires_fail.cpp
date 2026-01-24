// Test: requires requires pattern should fail when constraint is explicitly false
// This test should FAIL to compile
// Pattern: Using requires false to intentionally fail a constraint

template<typename T>
struct HasType {
    using type = T;
};

// Concept that will always fail
template<template<typename...> class Op, typename... Args>
concept AlwaysFails = requires {
    requires false;  // Explicitly fails
};

// Concept that checks if Op<Args...> is valid AND some condition
template<template<typename...> class Op, typename... Args>
concept MustHaveSmallSize = requires {
    typename Op<Args...>;
    requires sizeof(typename Op<Args...>::type) < 1;  // Impossible: no type has size < 1
};

// Function that requires the MustHaveSmallSize concept
template<template<typename...> class Op, typename... Args>
    requires MustHaveSmallSize<Op, Args...>
int use_constrained() {
    return 42;
}

int main() {
    // This SHOULD fail because sizeof(int) is 4, not < 1
    // The requires clause will evaluate to false
    int result = use_constrained<HasType, int>();
    
    return result;
}



