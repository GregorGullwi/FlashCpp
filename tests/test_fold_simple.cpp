// Simplified fold expression test with only arithmetic operators

// Test 1: Unary left fold with addition
template<typename... Args>
int sum(Args... args) {
    return (... + args);
}

// Test 2: Unary right fold with multiplication
template<typename... Args>
int product(Args... args) {
    return (args * ...);
}

// Test 3: Binary left fold with initial value
template<typename... Args>
int sum_with_init(int init, Args... args) {
    return (init + ... + args);
}

// Test 4: Binary left fold with subtraction
template<typename... Args>
int subtract_from_left(int init, Args... args) {
    return (init - ... - args);
}

// Test 5: Unary left fold with subtraction
template<typename... Args>
int sub_left(Args... args) {
    return (... - args);
}

// Test 6: Bitwise OR fold
template<typename... Args>
int bitwise_or(Args... args) {
    return (... | args);
}

int main() {
    int result = 0;
    
    // Test sum: 1 + 2 + 3 + 4 = 10
    result = result + sum(1, 2, 3, 4);
    // result = 10
    
    // Test product: 2 * 3 * 4 = 24
    result = result + product(2, 3, 4);
    // result = 34
    
    // Test sum_with_init: (100 + 1) + 2 + 3 = 106
    result = result + sum_with_init(100, 1, 2, 3);
    // result = 140
    
    // Test subtract_from_left: ((50 - 10) - 5) - 2 = 33
    result = result + subtract_from_left(50, 10, 5, 2);
    // result = 173
    
    // Test sub_left: (20 - 5) - 3 = 12
    result = result + sub_left(20, 5, 3);
    // result = 185
    
    // Test bitwise_or: 1 | 2 | 4 = 7
    result = result + bitwise_or(1, 2, 4);
    // result = 192
    
    return result;  // Expected: 192
}
