// Test SFINAE with type traits for pointers
// This tests accessing type members with pointer type checking

// enable_if
template<bool B, typename T = void>
struct enable_if {};

template<typename T>
struct enable_if<true, T> {
    using type = T;
};

// is_pointer trait
template<typename T>
struct is_pointer {
    static constexpr bool value = false;
};

template<typename T>
struct is_pointer<T*> {
    static constexpr bool value = true;
};

// Function for pointer types only
template<typename T>
typename enable_if<is_pointer<T>::value, int>::type
handle_pointer(T ptr) {
    return 100;
}

int main() {
    int x = 42;
    int* ptr = &x;
    
    // Test: Pointer type
    int result1 = handle_pointer(ptr);  // Should work: 100
    
    // Verify result
    return (result1 == 100) ? 0 : 1;
}
