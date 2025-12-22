// Test delegating + base constructor combination

struct Base {
    int value;
    Base() : value(0) {}
    Base(int v) : value(v) {}
};

struct Derived : Base {
    int extra;
    
    // Delegating constructor
    Derived() : Derived(100) {}
    
    // Base class constructor with member init
    Derived(int e) : Base(50), extra(e) {}
};

int main() {
    Derived d1;      // Delegates to Derived(100), which calls Base(50)
    Derived d2(200); // Calls Base(50), sets extra=200
    
    // d1: Base::value=50, extra=100  → 50+100 = 150
    // d2: Base::value=50, extra=200  → 50+200 = 250
    // Total: 150 + 250 = 400
    
    return d1.value + d1.extra + d2.value + d2.extra;
}
