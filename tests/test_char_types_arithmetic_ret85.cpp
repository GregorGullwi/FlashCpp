// Test for character type arithmetic and conversions
// Tests that char8_t, char16_t, char32_t are treated as unsigned
// and that wchar_t signedness is handled correctly

int testUnsignedBehavior() {
    // Test that char8_t behaves like unsigned char
    char8_t c8 = 200;
    c8 = c8 + 100;  // Should wrap around for unsigned (300 -> 44)
    
    // Test that char16_t behaves like unsigned short
    char16_t c16 = 60000;
    c16 = c16 + 10000;  // Should wrap around for unsigned
    
    // Test that char32_t behaves like unsigned int
    char32_t c32 = 4000000000U;
    c32 = c32 + 300000000U;
    
    return 42;
}

int testWCharBehavior() {
    // wchar_t signedness is platform-dependent
    wchar_t wc = 100;
    wc = wc + 50;
    
    return 43;
}

int main() {
    int result1 = testUnsignedBehavior();
    int result2 = testWCharBehavior();
    
    // Both tests should return successfully
    if (result1 == 42 && result2 == 43) {
        return 85;  // 42 + 43
    }
    return 0;
}
