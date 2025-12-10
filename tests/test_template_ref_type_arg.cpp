// Test for template specialization with reference type arguments
// This tests if T& can be passed as a template argument to a struct

template<typename T>
struct Wrapper {
    static constexpr int kind = 0;
};

// Specialization for reference types
template<typename T>
struct Wrapper<T&> {
    static constexpr int kind = 1;
};

int main() {
    // Test that reference types can be template arguments
    int x = Wrapper<int>::kind;      // Should be 0 (primary)
    int y = Wrapper<int&>::kind;     // Should be 1 (reference specialization)
    
    if (x != 0) return 1;
    if (y != 1) return 2;
    
    return 0;
}
