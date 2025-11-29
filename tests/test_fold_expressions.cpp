// Test C++17 fold expressions

// Test 1: Unary left fold with addition: (... + args)
template<typename... Args>
int sum(Args... args) {
    return (... + args);  // ((arg1 + arg2) + arg3) + ...
}

// Test 2: Unary right fold with multiplication: (args * ...)
template<typename... Args>
int product(Args... args) {
    return (args * ...);  // arg1 * (arg2 * (arg3 * ...))
}

// Test 3: Binary left fold with initial value: (init + ... + args)
template<typename... Args>
int sum_with_init(int init, Args... args) {
    return (init + ... + args);  // ((init + arg1) + arg2) + ...
}

// Test 4: Binary right fold with initial value: (init - ... - args)
template<typename... Args>
int subtract_from_left(int init, Args... args) {
    return (init - ... - args);  // ((init - arg1) - arg2) - arg3
}

// Test 5: Unary left fold with subtraction
template<typename... Args>
int sub_left(Args... args) {
    return (... - args);  // ((arg1 - arg2) - arg3) - ...
}

// Test 6: Logical AND fold
template<typename... Args>
int all(Args... args) {
    return (... && args);  // ((arg1 && arg2) && arg3) && ...
}

// Test 7: Logical OR fold
template<typename... Args>
int any(Args... args) {
    return (... || args);  // ((arg1 || arg2) || arg3) || ...
}

// Test 8: Bitwise OR fold
template<typename... Args>
int bitwise_or(Args... args) {
    return (... | args);
}

int main() {
    int result = 0;
    
    // Test sum: 1 + 2 + 3 + 4 = 10
    result = result + sum(1, 2, 3, 4);
    // result = 0 + 10 = 10
    
    // Test product: 2 * 3 * 4 = 24
    result = result + product(2, 3, 4);
    // result = 10 + 24 = 34
    
    // Test sum_with_init: (100 + 1) + 2 + 3 = 106
    result = result + sum_with_init(100, 1, 2, 3);
    // result = 34 + 106 = 140
    
    // Test subtract_from_left: ((50 - 10) - 5) - 2 = 33
    result = result + subtract_from_left(50, 10, 5, 2);
    // result = 140 + 33 = 173
    
    // Test sub_left: (20 - 5) - 3 = 12
    result = result + sub_left(20, 5, 3);
    // result = 173 + 12 = 185
    
    // Test all: 1 && 1 && 1 = 1
    result = result + (all(1, 1, 1) ? 1 : 0);
    // result = 185 + 1 = 186
    
    // Test any: 0 || 0 || 1 = 1
    result = result + (any(0, 0, 1) ? 1 : 0);
    // result = 186 + 1 = 187
    
    // Test bitwise_or: 1 | 2 | 4 = 7
    result = result + bitwise_or(1, 2, 4);
    // result = 187 + 7 = 194
    
    return result;  // Expected: 194
}
