// Test template partial specialization for const references
// Verifies that const T& specialization works correctly

template<typename T>
struct ConstRefTraits {
    static constexpr int is_const_ref = 0;
    static constexpr int value = 10;
};

// Partial specialization for const reference types
template<typename T>
struct ConstRefTraits<const T&> {
    static constexpr int is_const_ref = 1;
    static constexpr int value = 20;
};

int main() {
    // Primary template
    int a = ConstRefTraits<int>::is_const_ref;        // Should be 0
    int b = ConstRefTraits<int>::value;               // Should be 10
    
    // Const reference specialization
    int c = ConstRefTraits<const int&>::is_const_ref; // Should be 1
    int d = ConstRefTraits<const int&>::value;        // Should be 20
    
    // Verify values
    if (a != 0) return 1;
    if (b != 10) return 2;
    if (c != 1) return 3;
    if (d != 20) return 4;
    
    return 0;
}
