// Test without virtual functions
struct Base {
    int x;
    
    Base(int a) : x(a) {}
    
    int getValue() {  // Not virtual
        return x;
    }
};

struct Derived : public Base {
    int y;
    
    Derived(int a, int b) : Base(a), y(b) {}
    
    int getValue() {  // Not virtual, just shadows
        return x + y;
    }
};

int main() {
    Derived d(10, 20);
    return d.getValue();  // Should return 30 (no vtable needed)
}
