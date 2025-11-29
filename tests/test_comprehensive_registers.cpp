// Comprehensive test: Verify register allocation and spilling works correctly
// This test exercises both integer and floating-point register allocation
// with complex expressions that require many registers simultaneously

// Test 1: Integer register pressure with complex expression
int test_integer_registers() {
    int a = 1;
    int b = 2;
    int c = 3;
    int d = 4;
    int e = 5;
    int f = 6;
    int g = 7;
    int h = 8;
    int i = 9;
    int j = 10;
    
    // Complex expression that uses many registers at once
    int result = (a + b) * (c + d) + (e + f) * (g + h) + (i + j);
    // Expected: (1+2)*(3+4) + (5+6)*(7+8) + (9+10)
    //         = 3*7 + 11*15 + 19
    //         = 21 + 165 + 19
    //         = 205
    
    return result;
}

// Test 2: Floating-point register pressure with complex expression
float test_float_registers() {
    float a = 1.0f;
    float b = 2.0f;
    float c = 3.0f;
    float d = 4.0f;
    float e = 5.0f;
    float f = 6.0f;
    float g = 7.0f;
    float h = 8.0f;
    float i = 9.0f;
    float j = 10.0f;
    
    // Complex expression that uses many XMM registers at once
    float result = (a + b) * (c + d) + (e + f) * (g + h) + (i + j);
    // Expected: (1+2)*(3+4) + (5+6)*(7+8) + (9+10)
    //         = 3*7 + 11*15 + 19
    //         = 21 + 165 + 19
    //         = 205.0
    
    return result;
}

// Test 3: Mixed integer and float operations
int test_mixed_operations() {
    int x = 10;
    int y = 20;
    float a = 5.0f;
    float b = 2.0f;
    
    // This should trigger both GPR and XMM register allocation
    int int_result = x + y;  // 30
    float float_result = a * b;  // 10.0
    
    // Convert float to int and combine
    int combined = int_result + float_result;  // 30 + 10 = 40
    
    return combined;
}

// Test 4: Extreme register pressure - forces spilling
int test_extreme_pressure() {
    int v0 = 0;
    int v1 = 1;
    int v2 = 2;
    int v3 = 3;
    int v4 = 4;
    int v5 = 5;
    int v6 = 6;
    int v7 = 7;
    int v8 = 8;
    int v9 = 9;
    int v10 = 10;
    int v11 = 11;
    int v12 = 12;
    int v13 = 13;
    int v14 = 14;
    int v15 = 15;
    int v16 = 16;
    int v17 = 17;
    int v18 = 18;
    int v19 = 19;
    
    // Use all variables in a single expression to maximize register pressure
    int result = v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9 +
                 v10 + v11 + v12 + v13 + v14 + v15 + v16 + v17 + v18 + v19;
    // Expected: 0+1+2+...+19 = 190
    
    return result;
}

// Test 5: Extreme float register pressure - forces XMM spilling
float test_extreme_float_pressure() {
    float v0 = 0.0f;
    float v1 = 1.0f;
    float v2 = 2.0f;
    float v3 = 3.0f;
    float v4 = 4.0f;
    float v5 = 5.0f;
    float v6 = 6.0f;
    float v7 = 7.0f;
    float v8 = 8.0f;
    float v9 = 9.0f;
    float v10 = 10.0f;
    float v11 = 11.0f;
    float v12 = 12.0f;
    float v13 = 13.0f;
    float v14 = 14.0f;
    float v15 = 15.0f;
    float v16 = 16.0f;
    float v17 = 17.0f;
    float v18 = 18.0f;
    float v19 = 19.0f;
    
    // Use all variables in a single expression to maximize XMM register pressure
    float result = v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9 +
                   v10 + v11 + v12 + v13 + v14 + v15 + v16 + v17 + v18 + v19;
    // Expected: 0+1+2+...+19 = 190.0
    
    return result;
}

// Test 6: Nested expressions with register reuse
int test_nested_expressions() {
    int a = 5;
    int b = 10;
    int c = 15;
    int d = 20;
    
    // Nested expression that should allow register reuse
    int result = ((a + b) * (c + d)) + ((a * b) + (c * d));
    // Expected: ((5+10)*(15+20)) + ((5*10)+(15*20))
    //         = (15*35) + (50+300)
    //         = 525 + 350
    //         = 875
    
    return result;
}

// Test 7: Sequential operations (tests register release and reuse)
int test_sequential_operations() {
    int sum = 0;
    
    sum = sum + 1;   // 1
    sum = sum + 2;   // 3
    sum = sum + 3;   // 6
    sum = sum + 4;   // 10
    sum = sum + 5;   // 15
    sum = sum + 6;   // 21
    sum = sum + 7;   // 28
    sum = sum + 8;   // 36
    sum = sum + 9;   // 45
    sum = sum + 10;  // 55
    
    return sum;
}

// Main function that calls all tests
int main() {
    int test1 = test_integer_registers();      // Expected: 205
    float test2 = test_float_registers();      // Expected: 205.0
    int test3 = test_mixed_operations();       // Expected: 40
    int test4 = test_extreme_pressure();       // Expected: 190
    float test5 = test_extreme_float_pressure(); // Expected: 190.0
    int test6 = test_nested_expressions();     // Expected: 875
    int test7 = test_sequential_operations();  // Expected: 55
    
    // Combine all results for a final checksum
    int final_result = test1 + test3 + test4 + test6 + test7;
    // Expected: 205 + 40 + 190 + 875 + 55 = 1365
    
    // Add float results (converted to int)
    int float_sum = test2 + test5;  // 205 + 190 = 395
    
    // Final total
    int total = final_result + float_sum;  // 1365 + 395 = 1760
    
    return total;
}

