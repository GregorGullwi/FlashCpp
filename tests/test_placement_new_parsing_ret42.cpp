// Test: Placement new expressions with multiple arguments
// This tests both parsing operator declarations AND using placement new expressions
// Pattern: new (placement_args) Type(constructor_args)

typedef unsigned long size_t;

// Test 1: Custom placement new operator declarations with multiple args
struct CustomTag {};

inline void* operator new(size_t, void* ptr, CustomTag) noexcept {
    return ptr;
}

inline void operator delete(void*, void*, CustomTag) noexcept {
    // No-op
}

// Test 2: Standard placement new (single pointer argument)
inline void* operator new(size_t, void* ptr) noexcept {
    return ptr;
}

inline void operator delete(void*, void*) noexcept {
    // No-op
}

// Test 3: Array versions
inline void* operator new[](size_t, void* ptr, CustomTag) noexcept {
    return ptr;
}

inline void operator delete[](void*, void*, CustomTag) noexcept {
    // No-op
}

// Test 3: Simple struct for placement new testing
struct Point {
    int x;
    int y;
    
    Point(int px, int py) : x(px), y(py) {}
};

int main() {
    // Test actual placement new expressions
    char buffer[16];
    CustomTag tag;
    
    // Test: Placement new with multiple arguments
    Point* p = new ((void*)buffer, tag) Point(10, 32);
    
    // Return: 10 + 32 = 42
    return p->x + p->y;
}
