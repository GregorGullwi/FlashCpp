// Test file demonstrating C++14/20 lambda features

// C++14 Generic lambda with auto parameters
int test_generic_lambda() {
    auto add = [](auto a, auto b) { return a + b; };
    return add(2, 3);  // 5
}

// C++20 Template lambda  
int test_template_lambda() {
    auto identity = []<typename T>(T value) { return value; };
    return identity(42);  // 42
}

int main() {
    int result = 0;
    result += test_generic_lambda();      // 5
    result += test_template_lambda();     // 42
    return result;  // Expected: 47
}
