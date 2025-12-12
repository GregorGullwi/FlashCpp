// Test SFINAE with boolean logic
// This tests SFINAE with pre-computed boolean values

// enable_if
template<bool B, typename T = void>
struct enable_if {};

template<typename T>
struct enable_if<true, T> {
    using type = T;
};

// Type traits
template<typename T>
struct is_pointer {
    static constexpr bool value = false;
};

template<typename T>
struct is_pointer<T*> {
    static constexpr bool value = true;
};

// Pre-compute the condition for int pointers
template<typename T>
struct is_int_ptr {
    static constexpr bool value = is_pointer<T>::value;
};

// Function that works only for pointers
template<typename T>
typename enable_if<is_int_ptr<T>::value, int>::type
handle_ptr(T ptr) {
    return 300;
}

int main() {
    int x = 42;
    int* ptr = &x;
    
    // Test with pointer type
    int result = handle_ptr(ptr);
    
    // Expected: 300
    return (result == 300) ? 0 : 1;
}
