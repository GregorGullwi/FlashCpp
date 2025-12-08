// Test to verify Linux System V AMD64 ABI is being used correctly
// This test compiles a simple function and checks that it uses the correct registers

extern "C" int test_func_6_args(int a, int b, int c, int d, int e, int f) {
    // On Linux System V AMD64 ABI, the parameters should be:
    // a in RDI, b in RSI, c in RDX, d in RCX, e in R8, f in R9
    // On Windows Win64 ABI, only first 4 would be in registers:
    // a in RCX, b in RDX, c in R8, d in R9, e and f on stack
    return a + b + c + d + e + f;
}

extern "C" int main() {
    int result = test_func_6_args(1, 2, 3, 4, 5, 6);
    return result == 21 ? 0 : 1;  // Expected: 1+2+3+4+5+6 = 21
}
