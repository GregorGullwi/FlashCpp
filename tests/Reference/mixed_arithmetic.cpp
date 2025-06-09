// Test mixed integer and floating-point arithmetic
float test_int_to_float(int a, float b) {
    return a + b;  // int should be converted to float
}

double test_float_to_double(float a, double b) {
    return a + b;  // float should be converted to double
}

double test_int_to_double(int a, double b) {
    return a * b;  // int should be converted to double
}

float test_mixed_operations(int i, float f, double d) {
    float result = i + f;  // int + float -> float
    return result;
}

bool test_mixed_comparisons(int i, float f) {
    return i < f;  // int should be converted to float for comparison
}

int main() {
    float result1 = test_int_to_float(5, 3.14f);
    double result2 = test_float_to_double(2.5f, 1.5);
    double result3 = test_int_to_double(10, 2.5);
    bool result4 = test_mixed_comparisons(3, 3.5f);
    return 0;
}
