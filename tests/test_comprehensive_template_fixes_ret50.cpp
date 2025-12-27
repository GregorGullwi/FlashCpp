// Comprehensive test: Both constexpr typename and sizeof in template defaults
// This tests both fixes working together in a realistic scenario

template<typename T>
struct type_wrapper {
    using type = int;
};

// Pattern 1: sizeof in template parameter default
template<typename T, int Size = sizeof(int)>
struct sized_wrapper {
    static constexpr int size = Size;
};

// Pattern 2: constexpr typename in multi-line return type
template<typename T>
    constexpr typename type_wrapper<T>::type
    get_value(T /*unused*/) {
    return 42;
}

int main() {
    // Test pattern 1: sizeof in default
    constexpr int s1 = sized_wrapper<int>::size;  // Should be 4
    
    // Test pattern 2: constexpr typename
    constexpr int v1 = get_value(10);  // Should be 42
    
    // Combine results: 4 + 42 = 46
    return s1 + v1;
}

