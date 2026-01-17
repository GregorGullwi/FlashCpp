// Basic inheritance tests for FlashCpp

// Test 1: Simple single inheritance
struct Base1 {
    int x;
    
    Base1(int v) : x(v) {}
};

struct Derived1 : public Base1 {
    int y;
    
    Derived1(int a, int b) : Base1(a), y(b) {}
};

int test_simple_inheritance() {
    Derived1 d(10, 20);
    return d.x + d.y;  // Expected: 30
}

// Test 2: Implicit base constructor
struct Base2 {
    int x;
    
    Base2() : x(42) {}
};

struct Derived2 : public Base2 {
    int y;
    
    Derived2() : y(10) {}  // Implicitly calls Base2()
};

int test_implicit_base_ctor() {
    Derived2 d;
    return d.x + d.y;  // Expected: 52
}

// Test 3: Member access through inheritance
struct Base3 {
    int x;
    
    Base3(int v) : x(v) {}
    
    int getX() {
        return x;
    }
};

struct Derived3 : public Base3 {
    int y;
    
    Derived3(int a, int b) : Base3(a), y(b) {}
    
    int sum() {
        return x + y;  // Access base class member directly
    }
    
    int sumViaMethod() {
        return getX() + y;  // Call base class method
    }
};

int test_member_access() {
    Derived3 d(10, 20);
    return d.sum() + d.sumViaMethod();  // Expected: 60 (30 + 30)
}

// Test 4: Multi-level inheritance
struct BaseLevel1 {
    int x;
    
    BaseLevel1(int v) : x(v) {}
};

struct BaseLevel2 : public BaseLevel1 {
    int y;
    
    BaseLevel2(int a, int b) : BaseLevel1(a), y(b) {}
};

struct DerivedLevel3 : public BaseLevel2 {
    int z;
    
    DerivedLevel3(int a, int b, int c) : BaseLevel2(a, b), z(c) {}
};

int test_multi_level() {
    DerivedLevel3 d(10, 20, 30);
    return d.x + d.y + d.z;  // Expected: 60
}

// Test 5: Base class with multiple members
struct BaseMulti {
    int a;
    int b;
    int c;
    
    BaseMulti(int x, int y, int z) : a(x), b(y), c(z) {}
};

struct DerivedMulti : public BaseMulti {
    int d;
    int e;
    
    DerivedMulti(int v1, int v2, int v3, int v4, int v5) 
        : BaseMulti(v1, v2, v3), d(v4), e(v5) {}
};

int test_multiple_members() {
    DerivedMulti dm(1, 2, 3, 4, 5);
    return dm.a + dm.b + dm.c + dm.d + dm.e;  // Expected: 15
}

int main() {
    int result = 0;
    
    result += test_simple_inheritance();      // 30
    result += test_implicit_base_ctor();      // 52
    result += test_member_access();           // 60
    result += test_multi_level();             // 60
    result += test_multiple_members();        // 15
    
    return result;  // Expected: 217
}

