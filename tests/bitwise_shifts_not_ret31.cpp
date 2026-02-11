// Test shifts and NOT
int test_shifts_and_not() {
    // Shifts
    int left = 1 << 3;                  // 8
    int right = 32 >> 2;                // 8
    
    // NOT - this returns negative values as ones' complement
    unsigned char uc = ~0xF0;           // 0x0F = 15
    
    return left + right + uc;           // 8 + 8 + 15 = 31
}

int main() {
    return test_shifts_and_not();
}
