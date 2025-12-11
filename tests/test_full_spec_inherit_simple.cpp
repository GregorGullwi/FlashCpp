// Test full template specialization with inheritance - simple test
// This test works around the implicit constructor generation issue
// by ensuring base class constructors are defined

template<typename T>
struct Base {
    static inline int base_value = 10;
    
    // Define constructors to avoid undefined reference
    Base() {}
};

// Full specialization with inheritance - no additional members to avoid constructor generation
template<>
struct Base<int> : Base<char> {
    static inline int int_value = 20;
};

int get_int_value() {
    return Base<int>::int_value;
}

int get_base_value() {
    return Base<int>::base_value;
}

int main() {
    // Access member from the specialization
    int x = get_int_value();
    
    // Access inherited member from base class
    int y = get_base_value();
    
    // Should return 0 (20 + 10 - 30 = 0)
    return x + y - 30;
}
