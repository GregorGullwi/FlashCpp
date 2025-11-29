int main() {
    int mod = 17 % 5;       // 2
    int band = 12 & 10;     // 8  (1100 & 1010 = 1000)
    int bor = 12 | 10;      // 14 (1100 | 1010 = 1110)
    int bxor = 12 ^ 10;     // 6  (1100 ^ 1010 = 0110)
    
    // Total: 2 + 8 + 14 + 6 = 30
    return mod + band + bor + bxor;
}
