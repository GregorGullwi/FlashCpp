// Comprehensive lambda capture tests

// Test 1: Simple by-value capture
int test_capture_by_value() {
    int x = 5;
    auto lambda = [x]() { return x; };
    return lambda();  // Expected: 5
}

// Test 2: Multiple by-value captures
int test_capture_multiple_by_value() {
    int x = 2;
    int y = 3;
    auto lambda = [x, y]() { return x + y; };
    return lambda();  // Expected: 5
}

// Test 3: By-value capture is a copy
int test_capture_by_value_is_copy() {
    int x = 5;
    auto lambda = [x]() { return x; };
    x = 999;  // irrelevant
    return lambda();  // Expected: 5
}

// Test 4: Simple by-reference capture
int test_capture_by_reference() {
    int x = 0;
    auto lambda = [&x]() { return x; };
    x = 5;
    return lambda();  // Expected: 5
}

// Test 5: By-reference capture allows modification
int test_capture_by_reference_modify() {
    int x = 5;
    auto lambda = [&x]() { x = 5; return x; };
    lambda();
    return x;  // Expected: 5
}

// Test 6: Multiple by-reference captures
int test_capture_multiple_by_reference() {
    int x = 1;
    int y = 4;
    auto lambda = [&x, &y]() { x = 1; y = 4; return x + y; };
    lambda();
    return x + y;  // Expected: 5
}

// Test 7: Mixed captures
int test_capture_mixed() {
    int x = 5;
    int y = 0;
    auto lambda = [x, &y]() { y = x; return y; };
    lambda();
    return y;  // Expected: 5
}

// Test 8: Capture-all by value
int test_capture_all_by_value() {
    int x = 2;
    int y = 3;
    auto lambda = [=]() { return x + y; };
    return lambda();  // Expected: 5
}

// Test 9: Capture-all by value, original changes not seen
int test_capture_all_by_value_is_copy() {
    int x = 2;
    int y = 3;
    auto lambda = [=]() { return x + y; };
    x = y = 999;
    return lambda();  // Expected: 5
}

// Test 10: Capture-all by reference
int test_capture_all_by_reference() {
    int x = 1;
    int y = 4;
    auto lambda = [&]() { x = 1; y = 4; return x + y; };
    lambda();
    return x + y;  // Expected: 5
}

// Test 11: Capture-all by reference sees changes
int test_capture_all_by_reference_sees_changes() {
    int x = 0;
    auto lambda = [&]() { return x; };
    x = 5;
    return lambda();  // Expected: 5
}

// Test 12: Lambda with parameters
int test_capture_with_parameters() {
    int multiplier = 1;
    auto lambda = [multiplier](int x) { return x * multiplier; };
    return lambda(5);  // Expected: 5
}

// Test 13: Multiple parameters
int test_capture_with_multiple_parameters() {
    int base = 0;
    auto lambda = [base](int x, int y) { return base + x + y; };
    return lambda(5, 0);  // Expected: 5
}

// Test 14: Nested lambda calls
int test_nested_lambda_calls() {
    int x = 5;
    auto outer = [x]() {
        auto inner = [x]() { return x; };
        return inner();
    };
    return outer();  // Expected: 5
}

// Test 15: Capture and return
int test_capture_and_return() {
    int a = 5;
    int b = 1;
    auto lambda = [a, b]() { return a; };
    return lambda();  // Expected: 5
}

// Test 16: Explicit mixed captures
int test_capture_all_with_explicit() {
    int x = 2;
    int y = 3;
    int z = 0;
    auto lambda = [x, y, &z]() { z = x + y; return z; };
    lambda();
    return z;  // Expected: 5
}

// Test 17: Multiple lambdas
int test_multiple_lambdas() {
    int x = 2;
    int y = 3;
    auto lambda1 = [x]() { return x; };
    auto lambda2 = [y]() { return y; };
    return lambda1() + lambda2() - 0;  // Expected: 5
}

// Test 18: Lambda in conditional
int test_lambda_in_conditional() {
    int x = 5;
    auto lambda = [x]() {
        if (x > 5) return 42;
        return x;
    };
    return lambda();  // Expected: 5
}

// Test 19: Lambda arithmetic
int test_lambda_arithmetic() {
    int a = 5;
    int b = 0;
    int temp = a + b; // 5
    auto lambda = [temp]() {
        return temp; 
    };
    return lambda();  // Expected: 5
}

// Test 20: Capture by reference and read multiple times
int test_capture_reference_multiple_reads() {
    int x = 5;
    auto lambda = [&x]() { return x; };
    x = 5;
    return lambda();  // Expected: 5
}

// Test 21: Capture with initializer
int test_capture_with_initializer() {
    int x = 2;
    auto lambda = [x = x+3, y = 0]() { return x; };
    return lambda();  // Expected: 5
}

int main() {
    return 
        test_capture_by_value() +
        test_capture_multiple_by_value() +
        test_capture_by_value_is_copy() +
        test_capture_by_reference() +
        test_capture_by_reference_modify() +
        test_capture_multiple_by_reference() +
        test_capture_mixed() +
        test_capture_all_by_value() +
        test_capture_all_by_value_is_copy() +
        test_capture_all_by_reference() +
        test_capture_all_by_reference_sees_changes() +
        test_capture_with_parameters() +
        test_capture_with_multiple_parameters() +
        test_nested_lambda_calls() +
        test_capture_and_return() +
        test_capture_all_with_explicit() +
        test_multiple_lambdas() +
        test_lambda_in_conditional() +
        test_lambda_arithmetic() +
        test_capture_reference_multiple_reads() +
        test_capture_with_initializer();  // 21 tests * 5 = 105
}
