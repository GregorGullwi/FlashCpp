// Test case for 9 int parameters to verify integers still work correctly
// On Windows x64: 4 params in regs, 5 on stack
// On Linux x64: 6 params in regs, 3 on stack

int add_nine_ints(int a, int b, int c, int d, int e, int f, int g, int h, int i) {
    return a + b + c + d + e + f + g + h + i;
}

int main() {
    int result = add_nine_ints(1, 2, 3, 4, 5, 6, 7, 8, 9);
    // Expected: 1+2+3+4+5+6+7+8+9 = 45
    return result;  // Should return 45
}
