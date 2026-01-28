// Test double literal parsing and code generation

double test_double_literal() {
    return 3.14;  // No suffix = double by default
}

double test_double_with_exponent() {
    return 1.5e10;  // Scientific notation
}

double test_double_negative_exponent() {
    return 3.7e-5;
}

double test_double_arithmetic(double a, double b) {
    return a + b;
}

double test_mixed_operations() {
    double x = 10.5;
    double y = 2.5;
    return (x + y) * (x - y) / y;
}

int main() {
    double d1 = test_double_literal();
    double d2 = test_double_with_exponent();
    double d3 = test_double_negative_exponent();
    double d4 = test_double_arithmetic(5.5, 3.3);
    double d5 = test_mixed_operations();
    return 0;
}

