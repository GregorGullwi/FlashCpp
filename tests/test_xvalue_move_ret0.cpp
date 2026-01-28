// Test XValue support - std::move equivalent
// Casting to rvalue reference should produce xvalue

// Simple move helper (equivalent to std::move)
template<typename T>
T&& move(T& arg) {
    return static_cast<T&&>(arg);
}

// Test function that takes rvalue reference
int consume(int&& x) {
    return x + 10;
}

// Test function that takes lvalue reference  
int consume_lvalue(int& x) {
    return x + 20;
}

int main() {
    int value = 5;
    
    // Test 1: Cast to rvalue reference (xvalue)
    int result1 = consume(static_cast<int&&>(value));
    if (result1 != 15) return 1;  // 5 + 10 = 15
    
    // Test 2: Using move helper (also xvalue)
    int result2 = consume(move(value));
    if (result2 != 15) return 2;  // 5 + 10 = 15
    
    // Test 3: Lvalue reference for comparison
    int result3 = consume_lvalue(value);
    if (result3 != 25) return 3;  // 5 + 20 = 25
    
    return 0;  // Success
}
