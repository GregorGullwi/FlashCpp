// Test complex template parameter substitution
// Tests: const T&, T*, const T**, etc.

template<typename T>
struct Container {
    T value;
    T* ptr;
    const T& ref;
    
    Container(T v, T* p, const T& r) : value(v), ptr(p), ref(r) {}
    
    T getValue() { return value; }
    T* getPtr() { return ptr; }
    const T& getRef() { return ref; }
};

int main() {
    int x = 42;
    int y = 100;
    Container<int> c(x, &y, x);
    
    // Test member access
    int val = c.getValue();
    int* ptr = c.getPtr();
    const int& ref = c.getRef();
    
    // Verify values
    if (val != 42) return 1;
    if (*ptr != 100) return 2;
    if (ref != 42) return 3;
    
    return 0;  // Success
}
