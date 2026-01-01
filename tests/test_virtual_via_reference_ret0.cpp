// Test virtual function dispatch through a reference
// This tests that virtual calls work correctly when calling through a reference (ref.method())
// instead of a pointer (ptr->method())

struct Base {
    int value;
    
    Base(int v) : value(v) {}
    
    virtual int getValue() {
        return value;
    }
};

struct Derived : public Base {
    int extra;
    
    Derived(int v, int e) : Base(v), extra(e) {}
    
    int getValue() override {
        return value + extra;  // Should return 30 when called with (10, 20)
    }
};

int test_via_pointer() {
    Derived d(10, 20);
    Base* ptr = &d;
    return ptr->getValue();  // Should return 30 via virtual dispatch
}

int test_via_reference() {
    Derived d(10, 20);
    Base& ref = d;
    return ref.getValue();  // Should return 30 via virtual dispatch
}

int main() {
    int r1 = test_via_pointer();
    int r2 = test_via_reference();
    
    // Both should return 30 from Derived::getValue()
    // If reference dispatch is broken, r2 might return 10 from Base::getValue()
    if (r1 != 30) return 1;
    if (r2 != 30) return 2;
    
    return 0;  // Success
}
