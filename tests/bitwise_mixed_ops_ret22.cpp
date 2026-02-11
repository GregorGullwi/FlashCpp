// Test multiple bitwise operations
int test_operations() {
    char c = 0x0F & 0x07;               // 7
    short s = 0x00FF & 0x00F0;          // 240
    int i = 0x00FF & 0x0FF0;            // 240
    
    int or_test = 0x03 | 0x04;          // 7
    int xor_test = 0x0F ^ 0x07;         // 8
    
    return c + or_test + xor_test;      // 7 + 7 + 8 = 22
}

int main() {
    return test_operations();
}
