// Test mixing float and double types

float test_float_literal() {
    return 3.14f;  // 'f' suffix = float
}

double test_double_literal() {
    return 3.14;   // No suffix = double
}

float test_float_uppercase() {
    return 2.5F;   // Uppercase 'F' suffix
}

double test_scientific_double() {
    return 1.23e45;  // Large exponent
}

float test_scientific_float() {
    return 6.78e-9f;  // Small exponent with float
}

// Edge cases
float test_no_integer_part() {
    return .5f;  // No integer part
}

float test_no_fractional_part() {
    return 5.f;  // No fractional part
}

double test_integer_with_exponent() {
    return 5e10;  // Integer with exponent = double
}

int main() {
    float f1 = test_float_literal();
    double d1 = test_double_literal();
    float f2 = test_float_uppercase();
    double d2 = test_scientific_double();
    float f3 = test_scientific_float();
    float f4 = test_no_integer_part();
    float f5 = test_no_fractional_part();
    double d3 = test_integer_with_exponent();
    return 0;
}

