// Test case to expose 32-bit register spilling bug
// When we have many 32-bit variables and force register pressure,
// spilling a 32-bit value using 64-bit MOV can corrupt adjacent stack variables

int test_spill() {
    // Create enough variables to force register spilling
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
    
    // Force all variables to be used so they stay live
    // This should trigger register pressure and spilling
    int result = a + b + c + d + e + f + g + h + i + j;
    
    // If the spill corrupted adjacent variables, result will be wrong
    // Expected: 1+2+3+4+5+6+7+8+9+10 = 55
    return result;
}

int main() {
    return test_spill();
}
