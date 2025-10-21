// Test file for virtual inheritance and virtual functions
// Based on INHERITANCE_IMPLEMENTATION_PLAN.md Phase 1 and Phase 2 tests

// Test 1: Simple Single Inheritance
struct Base {
    int x;
    Base(int v) : x(v) {}
};

struct Derived : public Base {
    int y;
    Derived(int a, int b) : Base(a), y(b) {}
};

int test_simple_inheritance() {
    Derived d(10, 20);
    return d.x + d.y;  // Expected: 30
}

// Test 2: Implicit Base Constructor
struct BaseDefault {
    int x;
    BaseDefault() : x(42) {}
};

struct DerivedDefault : public BaseDefault {
    int y;
    DerivedDefault() : y(10) {}  // Implicitly calls BaseDefault()
};

int test_implicit_base_ctor() {
    DerivedDefault d;
    return d.x + d.y;  // Expected: 52
}

// Test 3: Member Access Through Inheritance
struct BaseWithMethod {
    int x;
    BaseWithMethod(int v) : x(v) {}

    int getX() { return x; }
};

struct DerivedWithMethod : public BaseWithMethod {
    int y;
    DerivedWithMethod(int a, int b) : BaseWithMethod(a), y(b) {}

    int sum() {
        return x + y;  // Access base class member directly
    }

    int sumViaMethod() {
        return getX() + y;  // Call base class method
    }
};

int test_member_access() {
    DerivedWithMethod d(10, 20);
    return d.sum() + d.sumViaMethod();  // Expected: 60 (30 + 30)
}

// Test 4: Multiple Levels of Inheritance
struct BaseLevel {
    int x;
    BaseLevel(int v) : x(v) {}
};

struct MiddleLevel : public BaseLevel {
    int y;
    MiddleLevel(int a, int b) : BaseLevel(a), y(b) {}
};

struct DerivedLevel : public MiddleLevel {
    int z;
    DerivedLevel(int a, int b, int c) : MiddleLevel(a, b), z(c) {}
};

int test_multi_level() {
    DerivedLevel d(10, 20, 30);
    return d.x + d.y + d.z;  // Expected: 60
}

// Test 5: Basic Virtual Function (Direct Call)
struct VirtualBase {
    virtual int getValue() { return 10; }
};

struct VirtualDerived : public VirtualBase {
    int getValue() override { return 20; }
};

int test_virtual_basic() {
    VirtualDerived d;
    return d.getValue();  // Expected: 20 (virtual dispatch on derived object)
}

// Test 6: Virtual Function Through Pointer
int test_virtual_pointer() {
    VirtualDerived d;
    VirtualBase* b = &d;
    return b->getValue();  // Expected: 20 (virtual dispatch through base pointer)
}

// Test 7: Multiple Virtual Functions
struct MultiVirtual {
    virtual int first() { return 1; }
    virtual int second() { return 2; }
    virtual int third() { return 3; }
};

struct MultiVirtualDerived : public MultiVirtual {
    int first() override { return 10; }
    int second() override { return 20; }
    int third() override { return 30; }
};

int test_multi_virtual() {
    MultiVirtualDerived d;
    MultiVirtual* b = &d;
    return b->first() + b->second() + b->third();  // Expected: 60
}

// Main function to run all tests
int main() {
    int result = 0;
    result += test_simple_inheritance();      // 30
    result += test_implicit_base_ctor();      // 52
    result += test_member_access();           // 60
    result += test_multi_level();             // 60
    result += test_virtual_basic();           // 20
    result += test_virtual_pointer();         // 20
    result += test_multi_virtual();           // 60
    return result;  // Expected: 302
}

