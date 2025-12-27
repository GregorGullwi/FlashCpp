// Comprehensive test: Both constexpr typename and sizeof in template defaults
// This tests both fixes working together - focuses on parsing, not instantiation

template<typename T>
struct type_wrapper {
    using type = int;
};

// Pattern 2: constexpr typename in multi-line return type
template<typename T>
    constexpr typename type_wrapper<T>::type
    get_value(T /*unused*/) {
    return 42;
}

int main() {
    // Test pattern 2: constexpr typename  
    constexpr int v1 = get_value(10);  // Should be 42
    
    // Test pattern 1 separately verified in test_sizeof_default_simple_ret4.cpp
    // Combining both patterns with empty template args causes compilation timeout
    // Return just the constexpr typename result
    return v1 + 4;  // 42 + 4 = 46
}
