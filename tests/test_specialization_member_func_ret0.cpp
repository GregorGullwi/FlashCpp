// Test member functions in template specializations
template<typename T>
struct Container {
    T value;
    
    void set(T v) {
        value = v;
    }
    
    T get() {
        return value;
    }
};

// Specialization for pointers
template<typename T>
struct Container<T*> {
    T* ptr;
    
    void set(T* p) {
        ptr = p;
    }
    
    T* get() {
        return ptr;
    }
};

int main() {
    // Test primary template
    Container<int> c1;
    c1.set(42);
    int result1 = c1.get();
    
    // Test pointer specialization
    int x = 100;
    Container<int*> c2;
    c2.set(&x);
    int* result2 = c2.get();
    
    return result1 + *result2 - 142;  // Should return 0
}
