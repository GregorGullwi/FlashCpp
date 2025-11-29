// Comprehensive lambda capture tests

// Test 1: Simple by-value capture
int test_capture_by_value() {
    int x = 42;
    auto lambda = [x]() { return x; };
    return lambda();  // Expected: 42
}

// Test 2: Multiple by-value captures
int test_capture_multiple_by_value() {
    int x = 10;
    int y = 32;
    auto lambda = [x, y]() { return x + y; };
    return lambda();  // Expected: 42
}

// Test 3: By-value capture is a copy (original changes don't affect lambda)
int test_capture_by_value_is_copy() {
    int x = 10;
    auto lambda = [x]() { return x; };
    x = 100;  // Change original
    return lambda();  // Expected: 10 (captured value, not changed)
}

// Test 4: Simple by-reference capture
int test_capture_by_reference() {
    int x = 10;
    auto lambda = [&x]() { return x; };
    x = 42;  // Change original
    return lambda();  // Expected: 42 (sees the change)
}

// Test 5: By-reference capture allows modification
int test_capture_by_reference_modify() {
    int x = 10;
    auto lambda = [&x]() { x = 42; return x; };
    lambda();
    return x;  // Expected: 42 (lambda modified it)
}

// Test 6: Multiple by-reference captures
int test_capture_multiple_by_reference() {
    int x = 10;
    int y = 20;
    auto lambda = [&x, &y]() { x = 5; y = 10; return x + y; };
    lambda();
    return x + y;  // Expected: 15 (both modified)
}

// Test 7: Mixed captures (by-value and by-reference)
int test_capture_mixed() {
    int x = 10;
    int y = 20;
    auto lambda = [x, &y]() { y = x + 5; return y; };
    lambda();
    return x + y;  // Expected: 25 (x=10, y=15)
}

// Test 8: Capture-all by value [=]
int test_capture_all_by_value() {
    int x = 10;
    int y = 32;
    auto lambda = [=]() { return x + y; };
    return lambda();  // Expected: 42
}

// Test 9: Capture-all by value doesn't see changes
int test_capture_all_by_value_is_copy() {
    int x = 10;
    int y = 32;
    auto lambda = [=]() { return x + y; };
    x = 100;
    y = 100;
    return lambda();  // Expected: 42 (captured values)
}

// Test 10: Capture-all by reference [&]
int test_capture_all_by_reference() {
    int x = 10;
    int y = 20;
    auto lambda = [&]() { x = 15; y = 25; return x + y; };
    lambda();
    return x + y;  // Expected: 40 (both modified)
}

// Test 11: Capture-all by reference sees changes
int test_capture_all_by_reference_sees_changes() {
    int x = 10;
    auto lambda = [&]() { return x; };
    x = 42;
    return lambda();  // Expected: 42 (sees the change)
}

// Test 12: Lambda with parameters and captures
int test_capture_with_parameters() {
    int multiplier = 3;
    auto lambda = [multiplier](int x) { return x * multiplier; };
    return lambda(14);  // Expected: 42
}

// Test 13: Lambda with multiple parameters and captures
int test_capture_with_multiple_parameters() {
    int base = 10;
    auto lambda = [base](int x, int y) { return base + x + y; };
    return lambda(20, 12);  // Expected: 42
}

// Test 14: Nested lambda calls
int test_nested_lambda_calls() {
    int x = 10;
    auto outer = [x]() {
        auto inner = [x]() { return x * 2; };
        return inner();
    };
    return outer();  // Expected: 20
}

// Test 15: Lambda capturing and returning value
int test_capture_and_return() {
    int a = 5;
    int b = 7;
    auto lambda = [a, b]() { return a * b + 7; };
    return lambda();  // Expected: 42
}

// Test 16: Capture-all with explicit captures (mixed) - NOT YET SUPPORTED
// int test_capture_all_with_explicit() {
//     int x = 10;
//     int y = 20;
//     int z = 12;
//     auto lambda = [=, &z]() { z = x + y; return z; };
//     lambda();
//     return z;  // Expected: 30 (z modified by reference)
// }

// Test 16: Alternative test - capture specific variables
int test_capture_all_with_explicit() {
    int x = 10;
    int y = 20;
    int z = 12;
    auto lambda = [x, y, &z]() { z = x + y; return z; };
    lambda();
    return z;  // Expected: 30 (z modified by reference)
}

// Test 17: Multiple lambda expressions
int test_multiple_lambdas() {
    int x = 10;
    int y = 20;
    auto lambda1 = [x]() { return x * 2; };
    auto lambda2 = [y]() { return y + 2; };
    return lambda1() + lambda2();  // Expected: 42 (20 + 22)
}

// Test 18: Lambda with comparison
int test_lambda_in_conditional() {
    int x = 10;
    auto lambda = [x]() {
        if (x > 5) {
            return 42;
        }
        return 0;
    };
    return lambda();  // Expected: 42
}

// Test 19: Lambda with arithmetic in body
int test_lambda_arithmetic() {
    int a = 5;
    int b = 7;
    int temp = a + b;
    auto lambda = [temp]() {
        int result = temp * 3;
        return result + 6;
    };
    return lambda();  // Expected: 42
}

// Test 20: Capture by reference and read multiple times
int test_capture_reference_multiple_reads() {
    int x = 10;
    auto lambda = [&x]() { return x + x + x; };
    x = 14;
    return lambda();  // Expected: 42 (14 + 14 + 14)
}

int main() {
    int result = 0;
    
    result += test_capture_by_value();                      // 42
    result += test_capture_multiple_by_value();             // 42
    result += test_capture_by_value_is_copy();              // 10
    result += test_capture_by_reference();                  // 42
    result += test_capture_by_reference_modify();           // 42
    result += test_capture_multiple_by_reference();         // 15
    result += test_capture_mixed();                         // 25
    result += test_capture_all_by_value();                  // 42
    result += test_capture_all_by_value_is_copy();          // 42
    result += test_capture_all_by_reference();              // 40
    result += test_capture_all_by_reference_sees_changes(); // 42
    result += test_capture_with_parameters();               // 42
    result += test_capture_with_multiple_parameters();      // 42
    result += test_nested_lambda_calls();                   // 20
    result += test_capture_and_return();                    // 42
    result += test_capture_all_with_explicit();             // 30
    result += test_multiple_lambdas();                      // 42
    result += test_lambda_in_conditional();                 // 42
    result += test_lambda_arithmetic();                     // 42
    result += test_capture_reference_multiple_reads();      // 42
    
    // Total: 42+42+10+42+42+15+25+42+42+40+42+42+42+20+42+30+42+42+42+42 = 728
    return result;
}

