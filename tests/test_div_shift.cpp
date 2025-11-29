int main() {
    // Signed division (different sizes)
    char c1 = 20 / 4;           // 5 (8-bit)
    short s1 = 60 / 3;          // 20 (16-bit)
    int i1 = 40 / 2;            // 20 (32-bit)
    
    // Unsigned division (different sizes)
    unsigned char uc1 = 25 / 5;         // 5 (8-bit)
    unsigned short us1 = 60 / 4;        // 15 (16-bit)
    unsigned int ui1 = 50 / 5;          // 10 (32-bit)
    
    // Signed shifts
    int sr = 64 >> 2;           // 16 (arithmetic right shift)
    int sl = 3 << 3;            // 24 (left shift)
    
    // Unsigned shifts  
    unsigned int usr = 80 >> 3;         // 10 (logical right shift)
    unsigned int usl = 5 << 2;          // 20 (left shift)
    
    // Total: 5+20+20 + 5+15+10 + 16+24 + 10+20 = 45+30+40+30 = 145
    return c1 + s1 + i1 + uc1 + us1 + ui1 + sr + sl + usr + usl;
}
