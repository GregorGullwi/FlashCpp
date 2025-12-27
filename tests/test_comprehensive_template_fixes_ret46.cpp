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

// Pattern 1: sizeof in template parameter default (non-dependent)
template<int Size = sizeof(int)>
struct sized_value {
    int get() { return Size; }
};

int main() {
    // Test pattern 2: constexpr typename  
    constexpr int v1 = get_value(10);  // Should be 42
    
    // Test pattern 1: sizeof in default
    sized_value<> sv;
    int s1 = sv.get();  // Should be 4
    
    // Combine results: 42 + 4 = 46
    return v1 + s1;
}
