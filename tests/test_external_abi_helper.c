// Helper functions compiled with standard compiler to verify ABI compatibility
// This file is compiled with gcc/clang and linked with code from FlashCpp

// Forward declare printf to avoid needing stdio.h
extern int printf(const char* format, ...);

// Test function with 6 integer parameters (all in registers on Linux)
int external_int_6_params(int a, int b, int c, int d, int e, int f) {
    printf("external_int_6_params: %d %d %d %d %d %d\n", a, b, c, d, e, f);
    return a + b + c + d + e + f;
}

// Test function with mixed int/float parameters (separate register pools)
double external_mixed_params(int a, double b, int c, double d) {
    printf("external_mixed_params: %d %.1f %d %.1f\n", a, b, c, d);
    return a + b + c + d;
}

// Test function with many parameters (tests stack passing)
int external_many_params(int p1, int p2, int p3, int p4, int p5, int p6, 
                         int p7, int p8, int p9, int p10) {
    printf("external_many_params: %d %d %d %d %d %d %d %d %d %d\n",
           p1, p2, p3, p4, p5, p6, p7, p8, p9, p10);
    return p1 + p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9 + p10;
}

// Test function with mixed types and stack overflow
double external_mixed_stack(int i1, double d1, int i2, double d2, int i3, double d3,
                            int i4, double d4, int i5, double d5) {
    printf("external_mixed_stack: i=%d d=%.1f i=%d d=%.1f i=%d d=%.1f i=%d d=%.1f i=%d d=%.1f\n",
           i1, d1, i2, d2, i3, d3, i4, d4, i5, d5);
    return i1 + d1 + i2 + d2 + i3 + d3 + i4 + d4 + i5 + d5;
}
