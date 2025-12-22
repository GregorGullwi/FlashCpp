// Nested classes tests for FlashCpp

// Test 1: Basic nested class declaration
class Outer1 {
public:
    class Inner {
    public:
        int value;
        Inner() : value(42) {}
    };
    
    Inner createInner() {
        return Inner();
    }
};

int test_basic_nested_class() {
    Outer1 o;
    Outer1::Inner i = o.createInner();
    return i.value;  // Expected: 42
}

// Test 2: Nested class accessing outer private members
class Outer2 {
private:
    int secret;
    
public:
    class Helper {
    public:
        int getSecret(Outer2& outer) {
            return outer.secret;  // Should compile - nested class can access outer private
        }
    };
    
    Outer2() : secret(99) {}
    
    int useHelper() {
        Helper h;
        return h.getSecret(*this);
    }
};

int test_nested_access_private() {
    Outer2 o;
    return o.useHelper();  // Expected: 99
}

// Test 3: Nested class with its own members
class Outer3 {
public:
    class Inner {
    private:
        int inner_data;
    public:
        Inner() : inner_data(50) {}
        int getData() { return inner_data; }
    };
    
    Inner createInner() {
        return Inner();
    }
};

int test_nested_with_members() {
    Outer3 o;
    Outer3::Inner i = o.createInner();
    return i.getData();  // Expected: 50
}

// Test 4: Nested class with constructor
class Outer4 {
public:
    class Inner {
    public:
        int x;
        int y;
        
        Inner(int a, int b) : x(a), y(b) {}
    };
};

int test_nested_constructor() {
    Outer4::Inner i(10, 20);
    return i.x + i.y;  // Expected: 30
}

// Test 5: Multiple nested classes
class Outer5 {
public:
    class Inner1 {
    public:
        int value1;
        Inner1() : value1(10) {}
    };
    
    class Inner2 {
    public:
        int value2;
        Inner2() : value2(20) {}
    };
};

int test_multiple_nested() {
    Outer5::Inner1 i1;
    Outer5::Inner2 i2;
    return i1.value1 + i2.value2;  // Expected: 30
}

// Test 6: Nested class with methods
class Outer6 {
private:
    int outer_value;
    
public:
    class Inner {
    public:
        int compute(Outer6& o) {
            return o.outer_value * 2;  // Access outer private member
        }
    };
    
    Outer6() : outer_value(25) {}
};

int test_nested_methods() {
    Outer6 o;
    Outer6::Inner i;
    return i.compute(o);  // Expected: 50
}

// Test 7: Nested class in struct (default public)
struct OuterStruct {
    struct InnerStruct {
        int value;
        InnerStruct() : value(77) {}
    };
};

int test_nested_in_struct() {
    OuterStruct::InnerStruct is;
    return is.value;  // Expected: 77
}

// Test 8: Nested class with private members
class Outer8 {
public:
    class Inner {
    private:
        int secret;
    public:
        Inner() : secret(88) {}
        int getSecret() { return secret; }
    };
};

int test_nested_private_members() {
    Outer8::Inner i;
    return i.getSecret();  // Expected: 88
}

// Test 9: Nested class accessing outer protected members
class Outer9 {
protected:
    int protected_value;
    
public:
    class Inner {
    public:
        int accessProtected(Outer9& o) {
            return o.protected_value;  // Should compile
        }
    };
    
    Outer9() : protected_value(111) {}
};

int test_nested_protected_access() {
    Outer9 o;
    Outer9::Inner i;
    return i.accessProtected(o);  // Expected: 111
}

// Test 10: Nested class with public and private sections
class Outer10 {
public:
    class Inner {
    private:
        int private_value;
    public:
        int public_value;
        
        Inner() : public_value(55), private_value(66) {}
        
        int getPrivateValue() {
            return private_value;
        }
    };
};

int test_nested_public_private() {
    Outer10::Inner i;
    return i.public_value + i.getPrivateValue();  // Expected: 121
}

// Test 11: Nested class with member functions
class Outer11 {
private:
    int data;
    
public:
    class Inner {
    public:
        void setData(Outer11& o, int value) {
            o.data = value;  // Access outer private
        }
        
        int getData(Outer11& o) {
            return o.data;  // Access outer private
        }
    };
    
    Outer11() : data(0) {}
};

int test_nested_member_functions() {
    Outer11 o;
    Outer11::Inner i;
    i.setData(o, 44);
    return i.getData(o);  // Expected: 44
}

// Test 12: Nested class with default constructor
class Outer12 {
public:
    class Inner {
    public:
        int value;
        Inner() : value(33) {}
    };
};

int test_nested_default_constructor() {
    Outer12::Inner i;
    return i.value;  // Expected: 33
}

// Test 13: Nested class accessing outer member functions
class Outer13 {
private:
    int getValue() { return 77; }
    
public:
    class Inner {
    public:
        int callOuterMethod(Outer13& o) {
            return o.getValue();  // Should compile - nested can access outer private
        }
    };
};

int test_nested_call_outer_method() {
    Outer13 o;
    Outer13::Inner i;
    return i.callOuterMethod(o);  // Expected: 77
}

// Test 14: Nested class with initialization
class Outer14 {
public:
    class Inner {
    public:
        int x;
        int y;
        Inner() : x(5), y(10) {}
    };
};

int test_nested_initialization() {
    Outer14::Inner i;
    return i.x * i.y;  // Expected: 50
}

// Test 15: Nested class in private section
class Outer15 {
private:
    class PrivateInner {
    public:
        int value;
        PrivateInner() : value(99) {}
    };
    
public:
    int usePrivateInner() {
        PrivateInner pi;
        return pi.value;
    }
};

int test_nested_private_section() {
    Outer15 o;
    return o.usePrivateInner();  // Expected: 99
}


int main() {
    return test_basic_nested_class();
}
