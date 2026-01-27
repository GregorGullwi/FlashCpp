// Test template partial specialization for lvalue references
// Verifies that T& specialization works correctly

template<typename T>
struct RefTraits {
    static constexpr int is_reference = 0;
    static constexpr int category = 0;
};

// Partial specialization for lvalue reference types
template<typename T>
struct RefTraits<T&> {
    static constexpr int is_reference = 1;
    static constexpr int category = 1;
};

int main() {
    // Primary template
    int a = RefTraits<int>::is_reference;     // Should be 0
    int b = RefTraits<int>::category;         // Should be 0
    
    // Reference specialization
    int c = RefTraits<int&>::is_reference;    // Should be 1
    int d = RefTraits<int&>::category;        // Should be 1
    
    // Verify values
    if (a != 0) return 1;
    if (b != 0) return 2;
    if (c != 1) return 3;
    if (d != 1) return 4;
    
    return 0;
}
