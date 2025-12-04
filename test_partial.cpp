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

int main() { return 0; }
