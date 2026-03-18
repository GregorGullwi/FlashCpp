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

typedef struct Big3 {
    int a;
    int b;
    int c;
} Big3;

typedef struct Big4 {
    int a;
    int b;
    int c;
    int d;
} Big4;

int flashcpp_sum_big3(Big3 value);
int flashcpp_big3_after_4_ints(int i1, int i2, int i3, int i4, Big3 value);
int flashcpp_big3_after_5_ints(int i1, int i2, int i3, int i4, int i5, Big3 value);
int flashcpp_sum_big4(Big4 value);
int flashcpp_big4_after_4_ints(int i1, int i2, int i3, int i4, Big4 value);
int flashcpp_big4_after_5_ints(int i1, int i2, int i3, int i4, int i5, Big4 value);

int external_sum_big3(Big3 value) {
    printf("external_sum_big3: %d %d %d\n", value.a, value.b, value.c);
    return value.a + value.b + value.c;
}

// Test: Big3 passed after 4 ints — still fits in registers (RDI-R8 for ints, R9+stack? no:
// i1→RDI, i2→RSI, i3→RDX, i4→RCX, Big3 low→R8, Big3 high→R9 — all 6 registers used)
int external_big3_after_4_ints(int i1, int i2, int i3, int i4, Big3 value) {
    printf("external_big3_after_4_ints: %d %d %d %d | %d %d %d\n",
           i1, i2, i3, i4, value.a, value.b, value.c);
    return i1 + i2 + i3 + i4 + value.a + value.b + value.c;
}

// Test: Big3 passed after 5 ints — Big3 overflows to the stack
// i1→RDI, i2→RSI, i3→RDX, i4→RCX, i5→R8 (5 regs used, only 1 left, Big3 needs 2 → stack)
int external_big3_after_5_ints(int i1, int i2, int i3, int i4, int i5, Big3 value) {
    printf("external_big3_after_5_ints: %d %d %d %d %d | %d %d %d\n",
           i1, i2, i3, i4, i5, value.a, value.b, value.c);
    return i1 + i2 + i3 + i4 + i5 + value.a + value.b + value.c;
}

int external_sum_big4(Big4 value) {
    printf("external_sum_big4: %d %d %d %d\n", value.a, value.b, value.c, value.d);
    return value.a + value.b + value.c + value.d;
}

int external_big4_after_4_ints(int i1, int i2, int i3, int i4, Big4 value) {
    printf("external_big4_after_4_ints: %d %d %d %d | %d %d %d %d\n",
           i1, i2, i3, i4, value.a, value.b, value.c, value.d);
    return i1 + i2 + i3 + i4 + value.a + value.b + value.c + value.d;
}

int external_big4_after_5_ints(int i1, int i2, int i3, int i4, int i5, Big4 value) {
    printf("external_big4_after_5_ints: %d %d %d %d %d | %d %d %d %d\n",
           i1, i2, i3, i4, i5, value.a, value.b, value.c, value.d);
    return i1 + i2 + i3 + i4 + i5 + value.a + value.b + value.c + value.d;
}

int external_call_flashcpp_big3(Big3 value) {
    printf("external_call_flashcpp_big3: %d %d %d\n", value.a, value.b, value.c);
    return flashcpp_sum_big3(value);
}

int external_call_flashcpp_big3_after_4_ints(int i1, int i2, int i3, int i4, Big3 value) {
    printf("external_call_flashcpp_big3_after_4_ints: %d %d %d %d | %d %d %d\n",
           i1, i2, i3, i4, value.a, value.b, value.c);
    return flashcpp_big3_after_4_ints(i1, i2, i3, i4, value);
}

int external_call_flashcpp_big3_after_5_ints(int i1, int i2, int i3, int i4, int i5, Big3 value) {
    printf("external_call_flashcpp_big3_after_5_ints: %d %d %d %d %d | %d %d %d\n",
           i1, i2, i3, i4, i5, value.a, value.b, value.c);
    return flashcpp_big3_after_5_ints(i1, i2, i3, i4, i5, value);
}

int external_call_flashcpp_big4(Big4 value) {
    printf("external_call_flashcpp_big4: %d %d %d %d\n", value.a, value.b, value.c, value.d);
    return flashcpp_sum_big4(value);
}

int external_call_flashcpp_big4_after_4_ints(int i1, int i2, int i3, int i4, Big4 value) {
    printf("external_call_flashcpp_big4_after_4_ints: %d %d %d %d | %d %d %d %d\n",
           i1, i2, i3, i4, value.a, value.b, value.c, value.d);
    return flashcpp_big4_after_4_ints(i1, i2, i3, i4, value);
}

int external_call_flashcpp_big4_after_5_ints(int i1, int i2, int i3, int i4, int i5, Big4 value) {
    printf("external_call_flashcpp_big4_after_5_ints: %d %d %d %d %d | %d %d %d %d\n",
           i1, i2, i3, i4, i5, value.a, value.b, value.c, value.d);
    return flashcpp_big4_after_5_ints(i1, i2, i3, i4, i5, value);
}
