// Test signed right shift behavior (arithmetic shift with sign extension)
// For negative numbers, right shift should preserve the sign bit

int test_negative_shift() {
    // Negative value right shift should do arithmetic shift (sign extend)
    int neg = -16;
    int result = neg >> 2;              // Should be -4 (sign extended)
    
    // Verify: -16 >> 2 = -4
    // In binary (two's complement):
    // -16 = 0xFFFFFFF0
    // -16 >> 2 = 0xFFFFFFFC = -4
    
    return result + 10;                 // -4 + 10 = 6
}

int test_positive_shift() {
    // Positive value right shift
    int pos = 64;
    int result = pos >> 2;              // Should be 16
    
    return result;                      // 16
}

int test_unsigned_large_shift() {
    // Unsigned right shift with high bit set (should zero-extend)
    unsigned int high = 0x80000000;     // High bit set
    unsigned int result = high >> 1;    // Should be 0x40000000 (logical shift)
    
    // Verify that high bit became 0 (not sign extended)
    // Result should be positive: 0x40000000 = 1073741824
    // Return modulo 256 for valid exit code
    return (result >> 24);              // 0x40 = 64
}

int main() {
    int a = test_negative_shift();      // 6
    int b = test_positive_shift();      // 16
    int c = test_unsigned_large_shift(); // 64
    
    return a + b + c;                   // 6 + 16 + 64 = 86
}
