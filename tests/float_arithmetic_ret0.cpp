float test_float_add(float a, float b) {
    return a + b;
}

float test_float_subtract(float a, float b) {
    return a - b;
}

float test_float_multiply(float a, float b) {
    return a * b;
}

float test_float_divide(float a, float b) {
    return a / b;
}

float test_complex_float_expression(float a, float b, float c) {
    return (a + b) * c - (a / b);
}

int main() {
    float result1 = test_float_add(3.14f, 2.86f);
    float result2 = test_float_multiply(2.5f, 4.0f);
    float result3 = test_complex_float_expression(10.0f, 2.0f, 3.0f);
    return 0;
}
