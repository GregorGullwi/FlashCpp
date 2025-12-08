// Test that stack overflow logic correctly handles mixed int/float types
extern "C" double test_mixed_overflow(
    int i1, double d1, int i2, double d2,
    int i3, double d3, int i4, double d4,
    int i5, double d5, int i6, double d6,
    int i7, double d7, int i8, double d8,
    int i9, double d9
);

extern "C" int main() {
    // Call function with many mixed parameters
    // Expected register allocation on Linux:
    // i1=RDI, d1=XMM0, i2=RSI, d2=XMM1, i3=RDX, d3=XMM2
    // i4=RCX, d4=XMM3, i5=R8, d5=XMM4, i6=R9, d6=XMM5
    // i7=STACK, d7=XMM6, i8=STACK, d8=XMM7, i9=STACK, d9=STACK
    
    double result = test_mixed_overflow(
        1, 1.5, 2, 2.5,
        3, 3.5, 4, 4.5,
        5, 5.5, 6, 6.5,
        7, 7.5, 8, 8.5,
        9, 9.5
    );
    
    // Expected sum: (1+1.5+2+2.5+3+3.5+4+4.5+5+5.5+6+6.5+7+7.5+8+8.5+9+9.5) = 90
    double expected = 90.0;
    if (result >= expected - 0.1 && result <= expected + 0.1) {
        return 0;  // Success
    }
    
    return 1;  // Failed
}
