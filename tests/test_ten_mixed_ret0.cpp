// Test with 10 mixed float/double parameters (will cause stack overflow on Linux)
float add_ten(
    float f1, double d1, float f2, double d2, float f3,
    double d3, float f4, double d4, float f5, double d5
) {
    double sum = static_cast<double>(f1) + d1 + static_cast<double>(f2) + d2 +
                 static_cast<double>(f3) + d3 + static_cast<double>(f4) + d4 +
                 static_cast<double>(f5) + d5;
    return static_cast<float>(sum);
}

int main() {
    // Test 1: literals - Expected: 1+2+3+4+5+6+7+8+9+10 = 55
    float r1 = add_ten(1.0f, 2.0, 3.0f, 4.0, 5.0f, 6.0, 7.0f, 8.0, 9.0f, 10.0);
    
    // Test 2: variables
    float f1 = 1.0f, f2 = 3.0f, f3 = 5.0f, f4 = 7.0f, f5 = 9.0f;
    double d1 = 2.0, d2 = 4.0, d3 = 6.0, d4 = 8.0, d5 = 10.0;
    float r2 = add_ten(f1, d1, f2, d2, f3, d3, f4, d4, f5, d5);
    
    // Test 3: from ints
    int i1 = 1, i2 = 2;
    float r3 = add_ten(
        static_cast<float>(i1), static_cast<double>(i2),
        static_cast<float>(i1 + 2), static_cast<double>(i2 + 2),
        static_cast<float>(i1 + 4), static_cast<double>(i2 + 4),
        static_cast<float>(i1 + 6), static_cast<double>(i2 + 6),
        static_cast<float>(i1 + 8), static_cast<double>(i2 + 8)
    );
    
    return 0;
}
