// Test SFINAE with std::enable_if_t alias pattern
// This is the modern C++14+ pattern used in standard libraries

// enable_if
template<bool B, typename T = void>
struct enable_if {};

template<typename T>
struct enable_if<true, T> {
    using type = T;
};

// Helper alias template (C++14 style)
template<bool B, typename T = void>
using enable_if_t = typename enable_if<B, T>::type;

// Type trait
template<typename T>
struct is_int {
    static constexpr bool value = false;
};

template<>
struct is_int<int> {
    static constexpr bool value = true;
};

// Function using enable_if_t (cleaner syntax)
template<typename T>
enable_if_t<is_int<T>::value, int>
modern_process(T val) {
    return val + 50;
}

int main() {
    // Test modern enable_if_t pattern
    int result = modern_process(42);
    
    // Expected: 42 + 50 = 92
    return (result == 92) ? 0 : 1;
}
