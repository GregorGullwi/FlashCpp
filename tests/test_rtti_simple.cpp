// Simple RTTI test - just check basic compilation

struct Base {
    int value;
    
    Base() : value(10) {}
    
    virtual int getValue() {
        return value;
    }
    
    virtual ~Base() {}
};

struct Derived : public Base {
    int extra;
    
    Derived() : extra(20) {
        value = 15;
    }
    
    virtual int getValue() {
        return value + extra;
    }
};

int test_basic() {
    Derived d;
    d.value = 5;
    d.extra = 10;
    
    return d.value + d.extra;  // Expected: 15
}

