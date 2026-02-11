// Comprehensive test for all bitwise operators with all integer types
// Tests: &, |, ^, ~, <<, >> for char, short, int, long (signed & unsigned)

int main() {
    // Test bitwise AND
    char c_and = 0x0F & 0x07;               // 7
    unsigned char uc_and = 0x0F & 0x07;     // 7
    short s_and = 0x0F & 0x07;              // 7
    unsigned short us_and = 0x0F & 0x07;    // 7
    int i_and = 0x0F & 0x07;                // 7
    unsigned int ui_and = 0x0F & 0x07;      // 7
    
    // Test bitwise OR
    char c_or = 0x03 | 0x04;                // 7
    unsigned char uc_or = 0x03 | 0x04;      // 7
    short s_or = 0x03 | 0x04;               // 7
    unsigned short us_or = 0x03 | 0x04;     // 7
    int i_or = 0x03 | 0x04;                 // 7
    unsigned int ui_or = 0x03 | 0x04;       // 7
    
    // Test bitwise XOR
    char c_xor = 0x0F ^ 0x08;               // 7
    unsigned char uc_xor = 0x0F ^ 0x08;     // 7
    short s_xor = 0x0F ^ 0x08;              // 7
    unsigned short us_xor = 0x0F ^ 0x08;    // 7
    int i_xor = 0x0F ^ 0x08;                // 7
    unsigned int ui_xor = 0x0F ^ 0x08;      // 7
    
    // Test bitwise NOT
    unsigned char uc_not = ~0xF8;           // 0x07 = 7
    unsigned short us_not = ~0xFFF8;        // 0x0007 = 7
    unsigned int ui_not = ~0xFFFFFFF8;      // 0x00000007 = 7
    
    // Test left shift
    char c_shl = 1 << 2;                    // 4
    unsigned char uc_shl = 1 << 2;          // 4
    short s_shl = 1 << 2;                   // 4
    unsigned short us_shl = 1 << 2;         // 4
    int i_shl = 1 << 2;                     // 4
    unsigned int ui_shl = 1 << 2;           // 4
    
    // Test right shift
    char c_shr = 16 >> 2;                   // 4
    unsigned char uc_shr = 16 >> 2;         // 4
    short s_shr = 16 >> 2;                  // 4
    unsigned short us_shr = 16 >> 2;        // 4
    int i_shr = 16 >> 2;                    // 4
    unsigned int ui_shr = 16 >> 2;          // 4
    
    // Test compound assignments
    int a = 15;
    a &= 7;                                 // 7
    
    int b = 8;
    b |= 4;                                 // 12
    
    int c = 15;
    c ^= 10;                                // 5
    
    int d = 2;
    d <<= 2;                                // 8
    
    int e = 32;
    e >>= 2;                                // 8
    
    // Sum: 
    // AND: 6*7 = 42
    // OR:  6*7 = 42
    // XOR: 6*7 = 42
    // NOT: 3*7 = 21
    // SHL: 6*4 = 24
    // SHR: 6*4 = 24
    // Compound: 7+12+5+8+8 = 40
    // Total: 42+42+42+21+24+24+40 = 235
    
    int result = 0;
    result += c_and + uc_and + s_and + us_and + i_and + ui_and;
    result += c_or + uc_or + s_or + us_or + i_or + ui_or;
    result += c_xor + uc_xor + s_xor + us_xor + i_xor + ui_xor;
    result += uc_not + us_not + ui_not;
    result += c_shl + uc_shl + s_shl + us_shl + i_shl + ui_shl;
    result += c_shr + uc_shr + s_shr + us_shr + i_shr + ui_shr;
    result += a + b + c + d + e;
    
    return result;
}
