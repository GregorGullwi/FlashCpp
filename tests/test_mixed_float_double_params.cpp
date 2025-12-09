// Test case for mixed float and double parameters with stack overflow
// This tests both float (32-bit) and double (64-bit) floating-point types
// On Linux: 8 float params go in XMM0-XMM7, rest on stack
// This has 12 float/double params total (8 in regs, 4 on stack)

float add_mixed_float_double(
    float f1, double d1, float f2, double d2,
    float f3, double d3, float f4, double d4,
    float f5, double d5, float f6, double d6
) {
    // Convert all to double for accurate addition, then back to float
    double sum = static_cast<double>(f1) + d1 + static_cast<double>(f2) + d2 +
                 static_cast<double>(f3) + d3 + static_cast<double>(f4) + d4 +
                 static_cast<double>(f5) + d5 + static_cast<double>(f6) + d6;
    return static_cast<float>(sum);
}

int main() {
    // Test 1: Call with literals
    // Expected: 1.0 + 2.0 + 3.0 + 4.0 + 5.0 + 6.0 + 7.0 + 8.0 + 9.0 + 10.0 + 11.0 + 12.0 = 78.0
    float result1 = add_mixed_float_double(
        1.0f, 2.0, 3.0f, 4.0,
        5.0f, 6.0, 7.0f, 8.0,
        9.0f, 10.0, 11.0f, 12.0
    );
    
    // Test 2: Call with variables (floats and doubles)
    float fvar1 = 1.5f;
    double dvar1 = 2.5;
    float fvar2 = 3.5f;
    double dvar2 = 4.5;
    float fvar3 = 5.5f;
    double dvar3 = 6.5;
    float fvar4 = 7.5f;
    double dvar4 = 8.5;
    float fvar5 = 9.5f;
    double dvar5 = 10.5;
    float fvar6 = 11.5f;
    double dvar6 = 12.5;
    
    // Expected: 1.5 + 2.5 + 3.5 + 4.5 + 5.5 + 6.5 + 7.5 + 8.5 + 9.5 + 10.5 + 11.5 + 12.5 = 84.0
    float result2 = add_mixed_float_double(
        fvar1, dvar1, fvar2, dvar2,
        fvar3, dvar3, fvar4, dvar4,
        fvar5, dvar5, fvar6, dvar6
    );
    
    // Test 3: Call with int variables converted to float/double
    int i1 = 2, i2 = 4, i3 = 6, i4 = 8, i5 = 10, i6 = 12;
    
    // Expected: 2.0 + 4.0 + 6.0 + 8.0 + 10.0 + 12.0 + 14.0 + 16.0 + 18.0 + 20.0 + 22.0 + 24.0 = 136.0
    float result3 = add_mixed_float_double(
        static_cast<float>(i1), static_cast<double>(i2),
        static_cast<float>(i3), static_cast<double>(i4),
        static_cast<float>(i5), static_cast<double>(i6),
        static_cast<float>(i1 + i6), static_cast<double>(i2 + i6),
        static_cast<float>(i3 + i6), static_cast<double>(i4 + i6),
        static_cast<float>(i5 + i6), static_cast<double>(i6 + i6)
    );
    
    // Test 4: Mix of literals, float vars, double vars, and int conversions
    float fmix = 1.0f;
    double dmix = 3.0;
    int imix = 5;
    
    // Expected: 1.0 + 3.0 + 5.0 + 2.0 + 1.0 + 3.0 + 5.0 + 2.0 + 1.0 + 3.0 + 5.0 + 2.0 = 33.0
    float result4 = add_mixed_float_double(
        fmix, dmix, static_cast<float>(imix), 2.0,
        fmix, dmix, static_cast<float>(imix), 2.0,
        fmix, dmix, static_cast<float>(imix), 2.0
    );
    
    // Validate all results
    float expected1 = 78.0f;
    float expected2 = 84.0f;
    float expected3 = 136.0f;
    float expected4 = 33.0f;
    
    float diff1 = result1 - expected1;
    if (diff1 < 0.0f) diff1 = -diff1;
    
    float diff2 = result2 - expected2;
    if (diff2 < 0.0f) diff2 = -diff2;
    
    float diff3 = result3 - expected3;
    if (diff3 < 0.0f) diff3 = -diff3;
    
    float diff4 = result4 - expected4;
    if (diff4 < 0.0f) diff4 = -diff4;
    
    if (diff1 < 0.01f && diff2 < 0.01f && diff3 < 0.01f && diff4 < 0.01f) {
        return 0;  // Success - all tests passed
    }
    
    // Return different error codes for different test failures
    if (diff1 >= 0.01f) return 1;
    if (diff2 >= 0.01f) return 2;
    if (diff3 >= 0.01f) return 3;
    if (diff4 >= 0.01f) return 4;
    
    return 99;  // Should not reach here
}
