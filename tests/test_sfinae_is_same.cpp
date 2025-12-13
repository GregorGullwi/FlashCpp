// Test SFINAE with is_same type trait
// This tests accessing type members that may not exist

// is_same type trait
template<typename T, typename U>
struct is_same {
    static constexpr bool value = false;
};

template<typename T>
struct is_same<T, T> {
    static constexpr bool value = true;
};

// enable_if implementation  
template<bool B, typename T = void>
struct enable_if {};

template<typename T>
struct enable_if<true, T> {
    using type = T;
};

// Function that only works with int
template<typename T>
typename enable_if<is_same<T, int>::value, int>::type
only_int(T val) {
    return val + 10;
}

// Function that only works with double  
template<typename T>
typename enable_if<is_same<T, double>::value, int>::type
only_double(T val) {
    return static_cast<int>(val) + 20;
}

int main() {
    // Test: Call only_int with int
    int result1 = only_int(42);  // Should work: 42 + 10 = 52
    
    // Test: Call only_double with double
    int result2 = only_double(3.14);  // Should work: 3 + 20 = 23
    
    // Verify results
    int total = result1 + result2;
    // Expected: 52 + 23 = 75
    
    return (total == 75) ? 0 : 1;
}
