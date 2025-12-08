// Test enhanced stack overflow with mixed int/float types
#include <stdio.h>

// Function with many mixed parameters to test stack overflow handling
// On Linux: 6 int regs (RDI-R9), 8 float regs (XMM0-7)
// This function has 10 ints and 9 doubles - both pools overflow
double test_mixed_overflow(
    int i1, double d1, int i2, double d2,
    int i3, double d3, int i4, double d4,
    int i5, double d5, int i6, double d6,
    int i7, double d7, int i8, double d8,
    int i9, double d9
) {
    printf("Received: i1=%d d1=%.1f i2=%d d2=%.1f i3=%d d3=%.1f\n", i1, d1, i2, d2, i3, d3);
    printf("          i4=%d d4=%.1f i5=%d d5=%.1f i6=%d d6=%.1f\n", i4, d4, i5, d5, i6, d6);
    printf("          i7=%d d7=%.1f i8=%d d8=%.1f i9=%d d9=%.1f\n", i7, d7, i8, d8, i9, d9);
    
    double sum = i1 + d1 + i2 + d2 + i3 + d3 + i4 + d4 + i5 + d5 + 
                 i6 + d6 + i7 + d7 + i8 + d8 + i9 + d9;
    return sum;
}
