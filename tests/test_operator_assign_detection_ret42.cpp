// Test: Improved operator= detection
// This tests distinguishing user-defined operator= from implicit ones
// The fix improved detection to avoid conflicts

struct SimpleClass {
    int value;
    
    // User-defined copy assignment operator
    SimpleClass& operator=(const SimpleClass& other) {
        value = other.value;
        return *this;
    }
};

struct ImplicitClass {
    int value;
    // No explicit operator= - compiler generates implicit one
};

struct ConstExprAssign {
    int value;
    
    // Test constexpr operator=
    constexpr ConstExprAssign& operator=(const ConstExprAssign& other) {
        value = other.value;
        return *this;
    }
};

int main() {
    // Test user-defined operator=
    SimpleClass s1{10};
    SimpleClass s2{32};
    s1 = s2;  // Uses user-defined operator=
    
    // Test implicit operator=
    ImplicitClass i1{5};
    ImplicitClass i2{37};
    i1 = i2;  // Uses compiler-generated operator=
    
    // Test constexpr operator=
    ConstExprAssign c1{1};
    ConstExprAssign c2{41};
    c1 = c2;
    
    // Return: s1.value + c1.value = 32 + 41 - 31 = 42
    return s1.value + c1.value - 31;
}
