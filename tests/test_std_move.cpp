// Test std::move support - should mark result as xvalue
// This tests the complete std::move integration

namespace std {
    // std::move implementation - casts to rvalue reference
    template<typename T>
    T&& move(T& arg) {
        return static_cast<T&&>(arg);
    }
}

// Test function that takes rvalue reference
int consume_rvalue(int&& x) {
    return x + 100;
}

// Test function that takes lvalue reference
int consume_lvalue(int& x) {
    return x + 200;
}

int main() {
    int value = 42;
    
    // Test 1: std::move should produce an xvalue
    int result1 = consume_rvalue(std::move(value));
    if (result1 != 142) return 1;  // 42 + 100 = 142
    
    // Test 2: Regular lvalue reference for comparison
    int result2 = consume_lvalue(value);
    if (result2 != 242) return 2;  // 42 + 200 = 242
    
    // Test 3: std::move with different value
    int value2 = 10;
    int result3 = consume_rvalue(std::move(value2));
    if (result3 != 110) return 3;  // 10 + 100 = 110
    
    return 0;  // Success
}
