// Simpler test for multi-level member access
struct Inner {
    int x;
    int y;
};

struct Outer {
    int a;
    Inner inner;
};

int main() {
    Outer obj;
    obj.a = 10;
    obj.inner.x = 20;
    obj.inner.y = 30;
    
    // Test: Get address of nested member
    int* ptr = &obj.inner.y;
    
    // Return the value through pointer
    return *ptr - 30;  // Should return 0
}
