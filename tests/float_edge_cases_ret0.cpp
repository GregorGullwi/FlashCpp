// Edge cases for floating-point literals

// Test various literal formats
float test_basic() { return 1.0f; }
float test_no_leading_zero() { return .5f; }
float test_no_trailing_digits() { return 5.f; }
float test_exponent_positive() { return 1.5e+10f; }
float test_exponent_negative() { return 1.5e-10f; }
float test_exponent_no_sign() { return 1.5e10f; }
float test_uppercase_E() { return 1.5E10f; }
float test_uppercase_F() { return 1.5F; }

// Integer with exponent becomes double
double test_int_exponent() { return 5e10; }

// Very small and very large values
float test_very_small() { return 1.0e-38f; }
float test_very_large() { return 3.4e38f; }

// Zero variations
float test_zero() { return 0.0f; }
float test_zero_no_decimal() { return 0.f; }
double test_zero_double() { return 0.0; }

int main() {
    float f1 = test_basic();
    float f2 = test_no_leading_zero();
    float f3 = test_no_trailing_digits();
    float f4 = test_exponent_positive();
    float f5 = test_exponent_negative();
    float f6 = test_exponent_no_sign();
    float f7 = test_uppercase_E();
    float f8 = test_uppercase_F();
    double d1 = test_int_exponent();
    float f9 = test_very_small();
    float f10 = test_very_large();
    float f11 = test_zero();
    float f12 = test_zero_no_decimal();
    double d2 = test_zero_double();
    return 0;
}

