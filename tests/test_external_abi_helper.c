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

typedef struct DoublePair {
	double a;
	double b;
} DoublePair;

typedef struct IntDouble {
	int a;
	double b;
} IntDouble;

typedef struct DoubleInt {
	double a;
	int b;
} DoubleInt;

#if defined(_MSC_VER)
typedef __declspec(align(16)) struct TailAlignedDouble {
	double value;
} TailAlignedDouble;
#else
typedef struct __attribute__((aligned(16))) TailAlignedDouble {
	double value;
} TailAlignedDouble;
#endif

#pragma pack(push, 1)
typedef struct PackedDouble {
	char tag;
	double value;
} PackedDouble;
#pragma pack(pop)

int flashcpp_sum_big3(Big3 value);
int flashcpp_big3_after_4_ints(int i1, int i2, int i3, int i4, Big3 value);
int flashcpp_big3_after_5_ints(int i1, int i2, int i3, int i4, int i5, Big3 value);
int flashcpp_sum_big4(Big4 value);
int flashcpp_big4_after_4_ints(int i1, int i2, int i3, int i4, Big4 value);
int flashcpp_big4_after_5_ints(int i1, int i2, int i3, int i4, int i5, Big4 value);
int flashcpp_check_double_pair(DoublePair value);
int flashcpp_check_int_double(IntDouble value);
int flashcpp_check_double_int(DoubleInt value);
DoublePair flashcpp_make_double_pair(double a, double b);
IntDouble flashcpp_make_int_double(int a, double b);
DoubleInt flashcpp_make_double_int(double a, int b);
int flashcpp_check_tail_aligned_double(TailAlignedDouble value);
TailAlignedDouble flashcpp_make_tail_aligned_double(double value);
int flashcpp_check_packed_double(PackedDouble value);
int flashcpp_int_double_after_8_doubles(
	double d1, double d2, double d3, double d4, double d5, double d6, double d7, double d8,
	IntDouble value, int tail);

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

int external_check_double_pair(DoublePair value) {
	return value.a == 10.5 && value.b == 31.5;
}

int external_check_int_double(IntDouble value) {
	return value.a == 10 && value.b == 32.0;
}

int external_check_double_int(DoubleInt value) {
	return value.a == 10.5 && value.b == 31;
}

int external_call_flashcpp_double_pair(DoublePair value) {
	return flashcpp_check_double_pair(value);
}

int external_call_flashcpp_int_double(IntDouble value) {
	return flashcpp_check_int_double(value);
}

int external_call_flashcpp_double_int(DoubleInt value) {
	return flashcpp_check_double_int(value);
}

DoublePair external_make_double_pair(double a, double b) {
	DoublePair value = {a, b};
	return value;
}

IntDouble external_make_int_double(int a, double b) {
	IntDouble value = {a, b};
	return value;
}

DoubleInt external_make_double_int(double a, int b) {
	DoubleInt value = {a, b};
	return value;
}

DoublePair external_call_flashcpp_make_double_pair(double a, double b) {
	return flashcpp_make_double_pair(a, b);
}

IntDouble external_call_flashcpp_make_int_double(int a, double b) {
	return flashcpp_make_int_double(a, b);
}

DoubleInt external_call_flashcpp_make_double_int(double a, int b) {
	return flashcpp_make_double_int(a, b);
}

int external_check_tail_aligned_double(TailAlignedDouble value) {
	return value.value == 42.5;
}

int external_call_flashcpp_tail_aligned_double(TailAlignedDouble value) {
	return flashcpp_check_tail_aligned_double(value);
}

TailAlignedDouble external_make_tail_aligned_double(double value) {
	TailAlignedDouble result = {value};
	return result;
}

TailAlignedDouble external_call_flashcpp_make_tail_aligned_double(double value) {
	return flashcpp_make_tail_aligned_double(value);
}

int external_check_packed_double(PackedDouble value) {
	return value.tag == 10 && value.value == 32.0;
}

int external_call_flashcpp_packed_double(PackedDouble value) {
	return flashcpp_check_packed_double(value);
}

int external_int_double_after_8_doubles(
	double d1, double d2, double d3, double d4, double d5, double d6, double d7, double d8,
	IntDouble value, int tail) {
	return (int)(d1 + d2 + d3 + d4 + d5 + d6 + d7 + d8) + value.a + (int)value.b + tail;
}

int external_call_flashcpp_int_double_after_8_doubles(
	double d1, double d2, double d3, double d4, double d5, double d6, double d7, double d8,
	IntDouble value, int tail) {
	return flashcpp_int_double_after_8_doubles(
		d1, d2, d3, d4, d5, d6, d7, d8, value, tail);
}
