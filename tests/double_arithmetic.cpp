double test_double_add(double a, double b) {
    return a + b;
}

double test_double_subtract(double a, double b) {
    return a - b;
}

double test_double_multiply(double a, double b) {
    return a * b;
}

double test_double_divide(double a, double b) {
    return a / b;
}

double test_complex_double_expression(double a, double b, double c) {
    return (a + b) * c - (a / b);
}

int main() {
    double result1 = test_double_add(3.141592653589793, 2.718281828459045);
    double result2 = test_double_multiply(2.5, 4.0);
    double result3 = test_complex_double_expression(10.0, 2.0, 3.0);
    return 0;
}
