// Test cases for decltype keyword support

int test_decltype_basic() {
    int x = 42;
    decltype(x) y = 10;
    return y;  // Should return 10
}

int test_decltype_expression() {
    int a = 5;
    int b = 10;
    decltype(a + b) result = a + b;
    return result;  // Should return 15
}

double test_decltype_mixed() {
    int x = 5;
    double y = 2.5;
    decltype(x + y) result = x + y;  // Should be double
    return result;  // Should return 7.5
}

int test_decltype_variable() {
    int value = 100;
    decltype(value) copy = value;
    return copy;  // Should return 100
}

int test_decltype_arithmetic() {
    int a = 20;
    int b = 3;
    decltype(a * b) product = a * b;
    return product;  // Should return 60
}

int test_decltype_comparison() {
    int x = 5;
    int y = 10;
    decltype(x < y) result = x < y;  // Should be bool (true = 1)
    return result;  // Should return 1
}

int test_decltype_multiple() {
    int a = 1;
    int b = 2;
    int c = 3;
    decltype(a + b) sum1 = a + b;
    decltype(b + c) sum2 = b + c;
    return sum1 + sum2;  // Should return 8
}

float test_decltype_float() {
    float x = 3.5f;
    float y = 2.0f;
    decltype(x * y) result = x * y;
    return result;  // Should return 7.0
}

int test_decltype_nested() {
    int a = 10;
    int b = 20;
    decltype(a + b) sum = a + b;
    decltype(sum * 2) doubled = sum * 2;
    return doubled;  // Should return 60
}

int test_decltype_with_literal() {
    decltype(42) value = 100;
    return value;  // Should return 100
}

int main() {
    int result = 0;
    result += test_decltype_basic();           // 10
    result += test_decltype_expression();      // 15
    result += test_decltype_variable();        // 100
    result += test_decltype_arithmetic();      // 60
    result += test_decltype_comparison();      // 1
    result += test_decltype_multiple();        // 8
    result += test_decltype_nested();          // 60
    result += test_decltype_with_literal();    // 100
    // Total: 354, return 354 % 256 = 98
    return result;
}
