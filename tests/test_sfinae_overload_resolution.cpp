// Test advanced SFINAE patterns
// Note: True overload resolution (same function name with SFINAE) is not yet supported
// This tests that SFINAE patterns work correctly with different function names

// enable_if
template<bool B, typename T = void>
struct enable_if {};

template<typename T>
struct enable_if<true, T> {
    using type = T;
};

// Type traits
template<typename T>
struct is_int {
    static constexpr bool value = false;
};

template<>
struct is_int<int> {
    static constexpr bool value = true;
};

template<typename T>
struct is_double {
    static constexpr bool value = false;
};

template<>
struct is_double<double> {
    static constexpr bool value = true;
};

// Test: Two different function names using SFINAE
// Note: Overloading with the same name doesn't work yet
template<typename T>
typename enable_if<is_int<T>::value, int>::type
process_int_type(T val) {
    return val + 100;
}

template<typename T>
typename enable_if<is_double<T>::value, int>::type
process_double_type(T) {
    return 200;
}

int main() {
    // Test with different function names
    int result1 = process_int_type(42);      // Should call int version: 42 + 100 = 142
    int result2 = process_double_type(3.14); // Should call double version: 200
    
    int total = result1 + result2;
    // Expected: 142 + 200 = 342
    
    return (total == 342) ? 0 : 1;
}
