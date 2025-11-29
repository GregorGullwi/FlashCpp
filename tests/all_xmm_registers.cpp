// Test that exercises all XMM registers (XMM0-XMM15)
// This tests the register allocator and floating-point operations
// to ensure proper REX prefix handling for XMM8-XMM15

float use_many_floats() {
    // Force the allocator to use many XMM registers
    float f0 = 1.0f;
    float f1 = 2.0f;
    float f2 = 3.0f;
    float f3 = 4.0f;
    float f4 = 5.0f;
    float f5 = 6.0f;
    float f6 = 7.0f;
    float f7 = 8.0f;
    float f8 = 9.0f;
    float f9 = 10.0f;
    float f10 = 11.0f;
    float f11 = 12.0f;
    float f12 = 13.0f;
    float f13 = 14.0f;
    float f14 = 15.0f;
    float f15 = 16.0f;
    
    // Perform operations to keep all registers live
    float sum = f0 + f1 + f2 + f3 + f4 + f5 + f6 + f7;
    float sum2 = f8 + f9 + f10 + f11 + f12 + f13 + f14 + f15;
    
    return sum + sum2;
}

double use_many_doubles() {
    // Force the allocator to use many XMM registers with doubles
    double d0 = 1.0;
    double d1 = 2.0;
    double d2 = 3.0;
    double d3 = 4.0;
    double d4 = 5.0;
    double d5 = 6.0;
    double d6 = 7.0;
    double d7 = 8.0;
    double d8 = 9.0;
    double d9 = 10.0;
    double d10 = 11.0;
    double d11 = 12.0;
    double d12 = 13.0;
    double d13 = 14.0;
    double d14 = 15.0;
    double d15 = 16.0;
    
    // Perform operations to keep all registers live
    double sum = d0 + d1 + d2 + d3 + d4 + d5 + d6 + d7;
    double sum2 = d8 + d9 + d10 + d11 + d12 + d13 + d14 + d15;
    
    return sum + sum2;
}

float test_float_division_with_many_vars() {
    float a = 100.0f;
    float b = 10.0f;
    float c = 5.0f;
    float d = 2.0f;
    float e = 1.0f;
    
    // Multiple divisions to test XMM register handling
    float r1 = a / b;
    float r2 = b / c;
    float r3 = c / d;
    float r4 = d / e;
    
    return r1 + r2 + r3 + r4;
}

double test_double_division_with_many_vars() {
    double a = 100.0;
    double b = 10.0;
    double c = 5.0;
    double d = 2.0;
    double e = 1.0;
    
    // Multiple divisions to test XMM register handling
    double r1 = a / b;
    double r2 = b / c;
    double r3 = c / d;
    double r4 = d / e;
    
    return r1 + r2 + r3 + r4;
}

int main() {
    float f = use_many_floats();
    double d = use_many_doubles();
    float fd = test_float_division_with_many_vars();
    double dd = test_double_division_with_many_vars();
    
    return (int)(f + d + fd + dd);
}
