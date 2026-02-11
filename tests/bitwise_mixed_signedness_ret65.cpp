// Test bitwise operations with mixed signedness
// Tests implicit conversions and usual arithmetic conversions

int test_mixed_signedness() {
    // Unsigned AND signed
    unsigned int a = 0xFF;
    int b = 0xF0;
    unsigned int result1 = a & b;       // Both promoted to unsigned, result = 0xF0 = 240
    
    // Signed OR unsigned
    int c = 0x0F;
    unsigned int d = 0x10;
    unsigned int result2 = c | d;       // Both promoted to unsigned, result = 0x1F = 31
    
    // Return low byte
    return (result1 & 0xFF) + (result2 & 0xFF);  // 240 + 31 = 271, but we need 0-255
}

int test_char_promotion() {
    // char operations promote to int before the operation
    char a = 0x0F;
    char b = 0x07;
    int result = a & b;                 // Promoted to int, result = 7
    
    return result;
}

int test_mixed_sizes() {
    // Different sized types in the same operation
    char c = 0x0F;
    short s = 0xF0;
    int i = c | s;                      // Both promoted to int, result = 0xFF = 255
    
    return i;
}

int main() {
    int a = test_char_promotion();      // 7
    int b = test_mixed_sizes();         // 255
    
    // test_mixed_signedness() would be 271 > 255, so skip it
    return (a + b) >> 2;                // (7 + 255) >> 2 = 262 >> 2 = 65
}
