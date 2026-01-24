// Test: void constexpr operator=() pattern
// This tests the pattern where specifiers appear after the return type
// Pattern: void constexpr operator=(params)
// This is different from the standard: operator=(params) const

struct Value {
    int data;
    
    // Test 1: void constexpr operator= (specifier after return type)
    void constexpr operator=(const Value& other) {
        data = other.data;
    }
};

struct Counter {
    int count;
    
    // Test 2: void operator= with constexpr after
    void constexpr operator=(const Counter& other) {
        count = other.count;
    }
    
    // Test 3: Regular constexpr member function for comparison
    constexpr int get() const {
        return count;
    }
};

struct Wrapper {
    int value;
    
    // Test 4: Multiple specifiers in unusual order
    void constexpr operator=(int v) {
        value = v;
    }
    
    int getValue() const {
        return value;
    }
};

int main() {
    Value v1{10};
    Value v2{20};
    v1 = v2;  // v1.data = 20
    
    Counter c1{5};
    Counter c2{15};
    c1 = c2;  // c1.count = 15
    
    Wrapper w;
    w = 7;  // w.value = 7
    
    // Return: 20 + 15 + 7 = 42
    return v1.data + c1.get() + w.getValue();
}
