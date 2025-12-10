// Test template partial specialization for rvalue references
// Verifies that T&& specialization works correctly

template<typename T>
struct RValueTraits {
    static constexpr int is_rvalue_ref = 0;
    static constexpr int tag = 100;
};

// Partial specialization for rvalue reference types
template<typename T>
struct RValueTraits<T&&> {
    static constexpr int is_rvalue_ref = 1;
    static constexpr int tag = 200;
};

int main() {
    // Primary template
    int a = RValueTraits<int>::is_rvalue_ref;      // Should be 0
    int b = RValueTraits<int>::tag;                // Should be 100
    
    // Rvalue reference specialization
    int c = RValueTraits<int&&>::is_rvalue_ref;    // Should be 1
    int d = RValueTraits<int&&>::tag;              // Should be 200
    
    // Verify values
    if (a != 0) return 1;
    if (b != 100) return 2;
    if (c != 1) return 3;
    if (d != 200) return 4;
    
    return 0;
}
