// Test full template specialization with inheritance
// This test verifies that full specializations can inherit from base classes
// We test with static members only to avoid constructor generation issues

template<typename T>
struct Base {
    static inline int base_value = 10;
};

// Full specialization with inheritance
template<>
struct Base<int> : Base<char> {
    static inline int int_value = 20;
};

int main() {
    // Access member from the specialization
    int x = Base<int>::int_value;
    
    // Access inherited member from base class
    int y = Base<int>::base_value;
    
    // Should return 0 (20 + 10 - 30 = 0)
    return x + y - 30;
}
