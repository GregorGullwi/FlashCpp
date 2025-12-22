// Test C++17 if constexpr - compile-time conditional compilation

// Test 1: Basic if constexpr with true condition
int test_basic_true() {
    if constexpr (true) {
        return 1;
    } else {
        return 2;
    }
}

// Test 2: Basic if constexpr with false condition
int test_basic_false() {
    if constexpr (false) {
        return 1;
    } else {
        return 2;
    }
}

// Test 3: if constexpr with constexpr expression (using global constexpr)
constexpr int global_value = 10;

int test_constexpr_expr() {
    if constexpr (global_value > 5) {
        return 100;
    } else {
        return 200;
    }
}

// Test 4: if constexpr without else branch (condition true)
int test_no_else_true() {
    int result = 0;
    if constexpr (true) {
        result = 42;
    }
    return result;
}

// Test 5: if constexpr without else branch (condition false)
int test_no_else_false() {
    int result = 99;
    if constexpr (false) {
        result = 42;
    }
    return result;
}

// Test 6: Nested if constexpr
int test_nested() {
    if constexpr (true) {
        if constexpr (false) {
            return 1;
        } else {
            return 2;
        }
    } else {
        return 3;
    }
}

// Test 7: if constexpr with arithmetic expression
constexpr int const_a = 5;
constexpr int const_b = 10;

int test_arithmetic() {
    if constexpr (const_a + const_b > 10) {
        return const_a + const_b;  // Should return 15
    } else {
        return 0;
    }
}

// Test 8: if constexpr with comparison
constexpr bool const_flag = (3 < 5);

int test_comparison() {
    if constexpr (const_flag) {
        return 77;
    } else {
        return 88;
    }
}

// Test 9: Multiple if constexpr in sequence
int test_multiple_sequential() {
    int result = 0;
    
    if constexpr (true) {
        result = result + 10;
    }
    
    if constexpr (false) {
        result = result + 100;
    }
    
    if constexpr (2 + 2 == 4) {
        result = result + 5;
    }
    
    return result;  // Should be 15 (10 + 5)
}

int main() {
    int total = 0;
    
    total = total + test_basic_true();        // +1 = 1
    total = total + test_basic_false();       // +2 = 3
    total = total + test_constexpr_expr();    // +100 = 103
    total = total + test_no_else_true();      // +42 = 145
    total = total + test_no_else_false();     // +99 = 244
    total = total + test_nested();            // +2 = 246
    total = total + test_arithmetic();        // +15 = 261
    total = total + test_comparison();        // +77 = 338
    total = total + test_multiple_sequential(); // +15 = 353
    
    return total;  // Expected: 353
}
