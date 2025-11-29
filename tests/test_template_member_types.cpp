// Simplified test for template parameter substitution
// Tests only member types (no function bodies)

template<typename T>
struct Container {
    T value;
    T* ptr;
    const T* const_ptr;
};

int main() {
    Container<int> c;
    c.value = 42;
    
    int x = 100;
    c.ptr = &x;
    c.const_ptr = &x;
    
    // Verify types work
    if (c.value != 42) return 1;
    if (*c.ptr != 100) return 2;
    if (*c.const_ptr != 100) return 3;
    
    return 0;  // Success
}
