// Test to verify System V AMD64 ABI handles mixed int/float arguments correctly
// This test ensures separate register pools for integers and floats

extern "C" double test_mixed_args(int a, double b, int c, double d) {
    // On Linux System V AMD64 ABI:
    // a in RDI (first int register)
    // b in XMM0 (first float register)
    // c in RSI (second int register)
    // d in XMM1 (second float register)
    //
    // On Windows (incorrectly):
    // a in RCX (first register)
    // b in XMM1 (second register - WRONG!)
    // c in R8 (third register)
    // d in XMM3 (fourth register - WRONG!)
    return a + b + c + d;
}

extern "C" int main() {
    double result = test_mixed_args(10, 20.5, 30, 40.5);
    // Expected: 10 + 20.5 + 30 + 40.5 = 101.0
    return (result > 100.9 && result < 101.1) ? 0 : 1;
}
