// Comprehensive C++20 Lambda Feature Test Suite
// Based on https://en.cppreference.com/w/cpp/language/lambda.html

// Test 1: Basic lambda with no captures
int test_basic_lambda() {
    auto lambda = []() { return 5; };
    return lambda();  // 5
}

// Test 2: Lambda with parameters
int test_lambda_with_params() {
    auto add = [](int a, int b) { return a + b; };
    return add(2, 3);  // 5
}

// Test 3: Capture by value
int test_capture_by_value() {
    int x = 3;
    auto lambda = [x]() { return x + 2; };
    return lambda();  // 5
}

// Test 4: Capture by reference
int test_capture_by_reference() {
    int x = 0;
    auto lambda = [&x]() { x = 5; };
    lambda();
    return x;  // 5
}

// Test 5: Capture-all by value
int test_capture_all_by_value() {
    int x = 2, y = 3;
    auto lambda = [=]() { return x + y; };
    return lambda();  // 5
}

// Test 6: Capture-all by reference
int test_capture_all_by_reference() {
    int x = 0;
    auto lambda = [&]() { x = 5; };
    lambda();
    return x;  // 5
}

// Test 7: Mixed captures - by value and by reference
int test_mixed_captures() {
    int x = 3;
    int y = 0;
    auto lambda = [x, &y]() { y = x + 2; };
    lambda();
    return y;  // 5
}

// Test 8: C++14 Init-capture with assignment
int test_init_capture() {
    int base = 3;
    auto lambda = [x = base + 2]() { return x; };
    return lambda();  // 5
}

// Test 9: C++14 Init-capture with literal value
int test_init_capture_modified() {
    auto lambda = [x = 3]() { return x + 2; };
    return lambda();  // 5
}

// Test 10: Mutable lambda
int test_mutable_lambda() {
    int x = 3;
    auto lambda = [x]() mutable { x += 2; return x; };
    return lambda();  // 5
}

// Test 11: Lambda with explicit return type
int test_explicit_return_type() {
    auto lambda = []() -> int { return 5; };
    return lambda();  // 5
}

// Test 12: C++14 Generic lambda with auto parameters
int test_generic_lambda() {
    auto add = [](auto a, auto b) { return a + b; };
    return add(2, 3);  // 5
}

// Test 13: Nested lambdas
int test_nested_lambdas() {
    int x = 3;
    auto outer = [x]() {
        auto inner = [x]() { return x + 2; };
        return inner();
    };
    return outer();  // 5
}

// Test 14: Lambda returning lambda
int test_lambda_returning_lambda() {
    auto maker = [](int offset) {
        return [offset](int base) { return base + offset; };
    };
    auto add2 = maker(2);
    return add2(3);  // 5
}

// Test 15: Immediately invoked lambda
int test_iife() {
    return []() { return 5; }();  // 5
}

// Test 16: Lambda with multiple statements
int test_multiple_statements() {
    auto lambda = []() {
        int x = 3;
        int y = 2;
        return x + y;
    };
    return lambda();  // 5
}

// Test 17: Capture with const qualifier
int test_const_capture() {
    const int x = 5;
    auto lambda = [x]() { return x; };
    return lambda();  // 5
}

// Test 18: Lambda in conditional
int test_lambda_in_conditional() {
    int condition = 1;
    auto result = condition ? 
        []() { return 5; }() : 
        []() { return 0; }();
    return result;  // 5
}

// Test 19: C++20 Template lambda (if supported)
// Note: This is a C++20 feature that may not be fully supported yet
int test_template_lambda() {
    auto lambda = []<typename T>(T value) { return value; };
    return lambda(5);  // 5
}

// Test 20: Multiple captures with different types
int test_multiple_different_captures() {
    int x = 2;
    double y = 3.0;
    auto lambda = [x, y]() { return x + (int)y; };
    return lambda();  // 5
}

// Test 21: Lambda with reference capture and modification
int test_ref_capture_modify() {
    int result = 0;
    auto lambda = [&result](int value) { result = value; };
    lambda(5);
    return result;  // 5
}

// Test 22: C++20 Capture-all with explicit this
struct TestStruct {
    int value = 5;
    
    int test_capture_all_with_this() {
        auto lambda = [=, this]() { return this->value; };
        return lambda();  // 5
    }
};

// Test 23: Explicit this capture
struct TestThis {
    int value = 5;
    
    int test_this_capture() {
        auto lambda = [this]() { return this->value; };
        return lambda();  // 5
    }
};

// Test 24: Lambda with multiple parameters
int test_multiple_params() {
    auto add3 = [](int a, int b, int c) { return a + b + c; };
    return add3(1, 2, 2);  // 5
}

// Test 25: Init-capture by reference
int test_init_capture_by_ref() {
    int x = 3;
    auto lambda = [&y = x]() { y += 2; };
    lambda();
    return x;  // 5
}

// Test 26: C++17 *this capture (copy all values from this)
struct TestCopyThis {
    int value = 5;
    
    auto get_lambda() {
        // Captures a copy of the entire object
        return [*this]() { return this->value; };
    }
};

int test_copy_this_capture() {
    TestCopyThis obj;
    auto lambda = obj.get_lambda();
    return lambda();  // 5
}

// Test 27: Recursive lambda using auto&& self parameter
int test_recursive_lambda() {
    auto factorial = [](auto&& self, int n) -> int {
        if (n <= 1) return 1;
        return n * self(self, n - 1);
    };
    // Calculate factorial(5) / factorial(4) = 5
    return factorial(factorial, 5) / factorial(factorial, 4);  // 5
}

int main() {
    int result = 0;
    
    result += test_basic_lambda();                    // 5
    result += test_lambda_with_params();              // 5
    result += test_capture_by_value();                // 5
    result += test_capture_by_reference();            // 5
    result += test_capture_all_by_value();            // 5
    result += test_capture_all_by_reference();        // 5
    result += test_mixed_captures();                  // 5
    result += test_init_capture();                    // 5
    result += test_init_capture_modified();           // 5
    result += test_mutable_lambda();                  // 5
    result += test_explicit_return_type();            // 5
    result += test_generic_lambda();                  // 5
    result += test_nested_lambdas();                  // 5
    result += test_lambda_returning_lambda();         // 5
    result += test_iife();                            // 5
    result += test_multiple_statements();             // 5
    result += test_const_capture();                   // 5
    result += test_lambda_in_conditional();           // 5
    // result += test_template_lambda();                 // 5 (C++20 template lambda - commented until supported)
    result += test_multiple_different_captures();     // 5
    result += test_ref_capture_modify();              // 5
    
    TestStruct ts;
    result += ts.test_capture_all_with_this();        // 5
    
    TestThis tt;
    result += tt.test_this_capture();                 // 5
    
    result += test_multiple_params();                 // 5
    result += test_init_capture_by_ref();             // 5
    result += test_copy_this_capture();               // 5
    result += test_recursive_lambda();                // 5
    
    // Total: 26 tests * 5 = 130
    return result;
}
