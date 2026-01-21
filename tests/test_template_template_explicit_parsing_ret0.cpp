// Test case to verify template template argument parsing works correctly
// This test exercises the fix for Parser.cpp:30883 where template template
// arguments were previously stored as empty placeholders

template<template<typename> class Container, typename T>
int test_explicit_template_args() {
    // Function uses explicit template template argument
    return 42;
}

template<template<typename> class Wrapper, typename T>
struct MyStruct {
    using type = Wrapper<T>;
    int value = 99;
};

template<typename T>
class Vector {
public:
    T data;
    Vector() : data() {}
};

template<typename T>
class List {
public:
    T value;
    List() : value() {}
};

int main() {
    // Test 1: Function template with explicit template template arguments
    int result1 = test_explicit_template_args<Vector, int>();
    
    // Test 2: Class template with explicit template template arguments
    MyStruct<List, int> s;
    int result2 = s.value;
    
    // Test 3: Different template template argument
    int result3 = test_explicit_template_args<List, double>();
    
    // Verify results
    if (result1 != 42) return 1;
    if (result2 != 99) return 2;
    if (result3 != 42) return 3;
    
    return 0;  // All tests passed
}
