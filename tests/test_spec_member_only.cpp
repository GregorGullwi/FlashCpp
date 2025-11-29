// Test member functions in PARTIAL template specializations only
// (Primary template member functions don't work due to delayed instantiation architecture)

template<typename T>
struct Container;  // Primary template declared but not defined

// Specialization for pointers - THIS WORKS
template<typename T>
struct Container<T*> {
    T* ptr = nullptr;  // Initialize to avoid undefined behavior
    
    void set(T* p) {
        ptr = p;
    }
    
    T* get() {
        return ptr;
    }
};

int main() {
    // Test pointer specialization
    int x = 42;
    Container<int*> c;
    c.set(&x);
    int* result = c.get();
    
    return *result - 42;  // Should return 0
}
