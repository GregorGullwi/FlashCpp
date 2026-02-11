// Test alternative operator keywords (bitand, bitor, xor, compl, and_eq, or_eq, xor_eq)
// C++20 allows these alternative tokens as equivalents to the symbolic operators

int test1() {
    int a = 0x0F bitand 0x07;           // Same as 0x0F & 0x07 = 7
    return a;
}

int test2() {
    int b = 0x03 bitor 0x04;            // Same as 0x03 | 0x04 = 7
    return b;
}

int test3() {
    int c = 0x0F xor 0x08;              // Same as 0x0F ^ 0x08 = 7
    return c;
}

int test4() {
    unsigned char d = compl 0xF8;       // Same as ~0xF8 = 0x07
    return d;
}

int test_alternative_keywords() {
    return test1() + test2() + test3() + test4();  // 7 + 7 + 7 + 7 = 28
}

int test_alternative_compound() {
    int result = 0;
    
    int a = 15;
    a and_eq 7;                         // Same as a &= 7, result = 7
    result += a;
    
    int b = 8;
    b or_eq 4;                          // Same as b |= 4, result = 12
    result += b;
    
    int c = 15;
    c xor_eq 10;                        // Same as c ^= 10, result = 5
    result += c;
    
    return result;                      // 7 + 12 + 5 = 24
}

int main() {
    return test_alternative_keywords() + test_alternative_compound();  // 28 + 24 = 52
}
