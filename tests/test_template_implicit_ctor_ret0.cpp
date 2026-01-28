// Comprehensive test: templates work WITHOUT explicit constructors
// This test proves the documentation was wrong - templates work fine!

template<typename T>
struct Container {
    T* ptr;
    const T* const_ptr;
    T value;
    // NO explicit constructor!
};

template<typename T>
struct Simple {
    T data;
    // NO explicit constructor!
};

int main() {
    // All of these should work:
    Container<int> c1;       // ✓ Works without explicit ctor
    Container<float> c2;     // ✓ Works with different type
    Simple<int> s1;          // ✓ Works for simple template
    Simple<char> s2;         // ✓ Works with different type
    
    return 0;  // Success!
}
