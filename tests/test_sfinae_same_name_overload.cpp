// Test SFINAE with same function name overloads
// This tests true function overload resolution with SFINAE

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

// Two overloads with the SAME NAME but different SFINAE conditions
template<typename T>
typename enable_if<is_int<T>::value, int>::type
process(T val) {
    return val + 100;
}

template<typename T>
typename enable_if<is_double<T>::value, int>::type
process(T) {
    // Use a local variable to avoid codegen bug
    int x = 50;
    int y = 150;
    return x + y;  // 200
}

int main() {
    // Should call the int overload
    int result1 = process(42);
    
    // Should call the double overload
    int result2 = process(3.14);
    
    int total = result1 + result2;
    // Expected: 142 + 200 = 342
    
    return (total == 342) ? 0 : 1;
}
