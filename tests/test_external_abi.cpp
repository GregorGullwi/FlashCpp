// Test ABI compatibility by calling external functions compiled with standard compiler
// This verifies that FlashCpp's calling convention matches the platform ABI

// Declare external functions from test_external_abi_helper.c
extern "C" int external_int_6_params(int a, int b, int c, int d, int e, int f);
extern "C" double external_mixed_params(int a, double b, int c, double d);
extern "C" int external_many_params(int p1, int p2, int p3, int p4, int p5, int p6,
									int p7, int p8, int p9, int p10);
extern "C" double external_mixed_stack(int i1, double d1, int i2, double d2, int i3, double d3,
									   int i4, double d4, int i5, double d5);
struct Big3 {
	int a;
	int b;
	int c;
};
struct Big4 {
	int a;
	int b;
	int c;
	int d;
};
struct DoublePair {
	double a;
	double b;
};
struct IntDouble {
	int a;
	double b;
};
struct DoubleInt {
	double a;
	int b;
};
struct alignas(16) TailAlignedDouble {
	double value;
};
#pragma pack(push, 1)
struct PackedDouble {
	char tag;
	double value;
};
#pragma pack(pop)
extern "C" int external_sum_big3(Big3 value);
extern "C" int external_big3_after_4_ints(int i1, int i2, int i3, int i4, Big3 value);
extern "C" int external_big3_after_5_ints(int i1, int i2, int i3, int i4, int i5, Big3 value);
extern "C" int external_sum_big4(Big4 value);
extern "C" int external_big4_after_4_ints(int i1, int i2, int i3, int i4, Big4 value);
extern "C" int external_big4_after_5_ints(int i1, int i2, int i3, int i4, int i5, Big4 value);
extern "C" int external_call_flashcpp_big3(Big3 value);
extern "C" int external_call_flashcpp_big3_after_4_ints(int i1, int i2, int i3, int i4, Big3 value);
extern "C" int external_call_flashcpp_big3_after_5_ints(int i1, int i2, int i3, int i4, int i5, Big3 value);
extern "C" int external_call_flashcpp_big4(Big4 value);
extern "C" int external_call_flashcpp_big4_after_4_ints(int i1, int i2, int i3, int i4, Big4 value);
extern "C" int external_call_flashcpp_big4_after_5_ints(int i1, int i2, int i3, int i4, int i5, Big4 value);
extern "C" int external_check_double_pair(DoublePair value);
extern "C" int external_check_int_double(IntDouble value);
extern "C" int external_check_double_int(DoubleInt value);
extern "C" int external_call_flashcpp_double_pair(DoublePair value);
extern "C" int external_call_flashcpp_int_double(IntDouble value);
extern "C" int external_call_flashcpp_double_int(DoubleInt value);
extern "C" DoublePair external_make_double_pair(double a, double b);
extern "C" IntDouble external_make_int_double(int a, double b);
extern "C" DoubleInt external_make_double_int(double a, int b);
extern "C" DoublePair external_call_flashcpp_make_double_pair(double a, double b);
extern "C" IntDouble external_call_flashcpp_make_int_double(int a, double b);
extern "C" DoubleInt external_call_flashcpp_make_double_int(double a, int b);
extern "C" int external_check_tail_aligned_double(TailAlignedDouble value);
extern "C" int external_call_flashcpp_tail_aligned_double(TailAlignedDouble value);
extern "C" TailAlignedDouble external_make_tail_aligned_double(double value);
extern "C" TailAlignedDouble external_call_flashcpp_make_tail_aligned_double(double value);
extern "C" int external_check_packed_double(PackedDouble value);
extern "C" int external_call_flashcpp_packed_double(PackedDouble value);
extern "C" int external_int_double_after_8_doubles(
	double d1, double d2, double d3, double d4, double d5, double d6, double d7, double d8,
	IntDouble value, int tail);
extern "C" int external_call_flashcpp_int_double_after_8_doubles(
	double d1, double d2, double d3, double d4, double d5, double d6, double d7, double d8,
	IntDouble value, int tail);

extern "C" int flashcpp_sum_big3(Big3 value) {
	return value.a + value.b + value.c;
}

extern "C" int flashcpp_big3_after_4_ints(int i1, int i2, int i3, int i4, Big3 value) {
	return i1 + i2 + i3 + i4 + value.a + value.b + value.c;
}

extern "C" int flashcpp_big3_after_5_ints(int i1, int i2, int i3, int i4, int i5, Big3 value) {
	return i1 + i2 + i3 + i4 + i5 + value.a + value.b + value.c;
}

extern "C" int flashcpp_sum_big4(Big4 value) {
	return value.a + value.b + value.c + value.d;
}

extern "C" int flashcpp_big4_after_4_ints(int i1, int i2, int i3, int i4, Big4 value) {
	return i1 + i2 + i3 + i4 + value.a + value.b + value.c + value.d;
}

extern "C" int flashcpp_big4_after_5_ints(int i1, int i2, int i3, int i4, int i5, Big4 value) {
	return i1 + i2 + i3 + i4 + i5 + value.a + value.b + value.c + value.d;
}

extern "C" int flashcpp_check_double_pair(DoublePair value) {
	return value.a == 10.5 && value.b == 31.5;
}

extern "C" int flashcpp_check_int_double(IntDouble value) {
	return value.a == 10 && value.b == 32.0;
}

extern "C" int flashcpp_check_double_int(DoubleInt value) {
	return value.a == 10.5 && value.b == 31;
}

extern "C" DoublePair flashcpp_make_double_pair(double a, double b) {
	return DoublePair{a, b};
}

extern "C" IntDouble flashcpp_make_int_double(int a, double b) {
	return IntDouble{a, b};
}

extern "C" DoubleInt flashcpp_make_double_int(double a, int b) {
	return DoubleInt{a, b};
}

extern "C" int flashcpp_check_tail_aligned_double(TailAlignedDouble value) {
	return value.value == 42.5;
}

extern "C" TailAlignedDouble flashcpp_make_tail_aligned_double(double value) {
	return TailAlignedDouble{value};
}

extern "C" int flashcpp_check_packed_double(PackedDouble value) {
	return value.tag == 10 && value.value == 32.0;
}

extern "C" int flashcpp_int_double_after_8_doubles(
	double d1, double d2, double d3, double d4, double d5, double d6, double d7, double d8,
	IntDouble value, int tail) {
	return static_cast<int>(d1 + d2 + d3 + d4 + d5 + d6 + d7 + d8) + value.a +
		static_cast<int>(value.b) + tail;
}

extern "C" int main() {
	int result = 0;

	// Test 1: 6 integer parameters (all in registers on Linux: RDI, RSI, RDX, RCX, R8, R9)
	int test1 = external_int_6_params(1, 2, 3, 4, 5, 6);
	if (test1 != 21) {
		return 1; // Expected: 1+2+3+4+5+6 = 21
	}

	// Test 2: Mixed int/double parameters (separate register pools)
	// On Linux: a→RDI, b→XMM0, c→RSI, d→XMM1
	double test2 = external_mixed_params(10, 20.5, 30, 40.5);
	if (test2 < 100.9 || test2 > 101.1) {
		return 2; // Expected: 10+20.5+30+40.5 = 101.0
	}

	// Test 3: Many integer parameters (tests stack passing)
	// On Linux: p1-p6 in registers (RDI,RSI,RDX,RCX,R8,R9), p7-p10 on stack
	int test3 = external_many_params(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
	if (test3 != 55) {
		return 3; // Expected: sum(1..10) = 55
	}

	// Test 4: Mixed types with stack overflow (tests both register pools and stack)
	// On Linux: i1-i6 use RDI,RSI,RDX,RCX,R8,R9; d1-d5 use XMM0-XMM4
	// But we only have 6 int regs and 8 float regs, so some should go on stack
	double test4 = external_mixed_stack(1, 2.5, 3, 4.5, 5, 6.5, 7, 8.5, 9, 10.5);
	if (test4 < 57.4 || test4 > 57.6) {
		return 4; // Expected: 1+2.5+3+4.5+5+6.5+7+8.5+9+10.5 = 57.5
	}

	// Test 5: 12-byte struct parameter (must use two-register SysV ABI, not pointer convention)
	Big3 test5 = {10, 12, 20};
	if (external_sum_big3(test5) != 42) {
		return 5;
	}

	// Test 6: Big3 after 4 ints — Big3 still fits in registers (R8+R9)
	// i1→RDI, i2→RSI, i3→RDX, i4→RCX, Big3 low→R8, Big3 high→R9
	Big3 test6 = {100, 200, 300};
	if (external_big3_after_4_ints(1, 2, 3, 4, test6) != 610) {
		return 6; // Expected: 1+2+3+4+100+200+300 = 610
	}

	// Test 7: Big3 after 5 ints — Big3 overflows to the stack
	// i1→RDI, i2→RSI, i3→RDX, i4→RCX, i5→R8 (5 regs used, only 1 left, Big3 needs 2 → stack)
	Big3 test7 = {100, 200, 300};
	if (external_big3_after_5_ints(1, 2, 3, 4, 5, test7) != 615) {
		return 7; // Expected: 1+2+3+4+5+100+200+300 = 615
	}

	// Test 8: 16-byte struct parameter (must also use two-register SysV ABI, not pointer convention)
	Big4 test8 = {10, 12, 8, 12};
	if (external_sum_big4(test8) != 42) {
		return 8;
	}

	// Test 9: Big4 after 4 ints — Big4 still fits in registers (R8+R9)
	Big4 test9 = {100, 200, 300, 400};
	if (external_big4_after_4_ints(1, 2, 3, 4, test9) != 1010) {
		return 9;
	}

	// Test 10: Big4 after 5 ints — Big4 overflows to the stack
	Big4 test10 = {100, 200, 300, 400};
	if (external_big4_after_5_ints(1, 2, 3, 4, 5, test10) != 1015) {
		return 10;
	}

	// Test 11: External clang caller -> FlashCpp callee (12-byte struct in registers)
	Big3 test11 = {10, 12, 20};
	if (external_call_flashcpp_big3(test11) != 42) {
		return 11;
	}

	// Test 12: External clang caller -> FlashCpp callee (12-byte struct still in regs after 4 ints)
	Big3 test12 = {100, 200, 300};
	if (external_call_flashcpp_big3_after_4_ints(1, 2, 3, 4, test12) != 610) {
		return 12;
	}

	// Test 13: External clang caller -> FlashCpp callee (12-byte struct spills after 5 ints)
	Big3 test13 = {100, 200, 300};
	if (external_call_flashcpp_big3_after_5_ints(1, 2, 3, 4, 5, test13) != 615) {
		return 13;
	}

	// Test 14: External clang caller -> FlashCpp callee (16-byte struct in registers)
	Big4 test14 = {10, 12, 8, 12};
	if (external_call_flashcpp_big4(test14) != 42) {
		return 14;
	}

	// Test 15: External clang caller -> FlashCpp callee (16-byte struct still in regs after 4 ints)
	Big4 test15 = {100, 200, 300, 400};
	if (external_call_flashcpp_big4_after_4_ints(1, 2, 3, 4, test15) != 1010) {
		return 15;
	}

	// Test 16: External clang caller -> FlashCpp callee (16-byte struct spills after 5 ints)
	Big4 test16 = {100, 200, 300, 400};
	if (external_call_flashcpp_big4_after_5_ints(1, 2, 3, 4, 5, test16) != 1015) {
		return 16;
	}

	// Tests 17-19: FlashCpp caller -> platform compiler callee for all SysV
	// two-eightbyte register-class combinations.
	if (!external_check_double_pair(DoublePair{10.5, 31.5})) {
		return 17;
	}
	if (!external_check_int_double(IntDouble{10, 32.0})) {
		return 18;
	}
	if (!external_check_double_int(DoubleInt{10.5, 31})) {
		return 19;
	}

	// Tests 20-22: platform compiler caller -> FlashCpp callee.
	if (!external_call_flashcpp_double_pair(DoublePair{10.5, 31.5})) {
		return 20;
	}
	if (!external_call_flashcpp_int_double(IntDouble{10, 32.0})) {
		return 21;
	}
	if (!external_call_flashcpp_double_int(DoubleInt{10.5, 31})) {
		return 22;
	}

	// Tests 23-28: direct aggregate returns in both directions.
	DoublePair external_double_pair = external_make_double_pair(10.5, 31.5);
	if (external_double_pair.a != 10.5 || external_double_pair.b != 31.5) {
		return 23;
	}
	IntDouble external_int_double = external_make_int_double(10, 32.0);
	if (external_int_double.a != 10 || external_int_double.b != 32.0) {
		return 24;
	}
	DoubleInt external_double_int = external_make_double_int(10.5, 31);
	if (external_double_int.a != 10.5 || external_double_int.b != 31) {
		return 25;
	}
	DoublePair flashcpp_double_pair = external_call_flashcpp_make_double_pair(10.5, 31.5);
	if (flashcpp_double_pair.a != 10.5 || flashcpp_double_pair.b != 31.5) {
		return 26;
	}
	IntDouble flashcpp_int_double = external_call_flashcpp_make_int_double(10, 32.0);
	if (flashcpp_int_double.a != 10 || flashcpp_int_double.b != 32.0) {
		return 27;
	}
	DoubleInt flashcpp_double_int = external_call_flashcpp_make_double_int(10.5, 31);
	if (flashcpp_double_int.a != 10.5 || flashcpp_double_int.b != 31) {
		return 28;
	}

	// Tests 29-32: trailing padding remains NO_CLASS, so this 16-byte aggregate
	// consumes only one SSE register in both argument and return directions.
	if (!external_check_tail_aligned_double(TailAlignedDouble{42.5})) {
		return 29;
	}
	if (!external_call_flashcpp_tail_aligned_double(TailAlignedDouble{42.5})) {
		return 30;
	}
	TailAlignedDouble external_tail_aligned = external_make_tail_aligned_double(42.5);
	if (external_tail_aligned.value != 42.5) {
		return 31;
	}
	TailAlignedDouble flashcpp_tail_aligned = external_call_flashcpp_make_tail_aligned_double(42.5);
	if (flashcpp_tail_aligned.value != 42.5) {
		return 32;
	}

	// Tests 33-34: an unaligned aggregate is MEMORY-class even though it is
	// smaller than 16 bytes, so arguments use the overflow area.
	if (!external_check_packed_double(PackedDouble{10, 32.0})) {
		return 33;
	}
	if (!external_call_flashcpp_packed_double(PackedDouble{10, 32.0})) {
		return 34;
	}

	// Tests 35-36: the mixed aggregate needs one GPR and one SSE register. With
	// all SSE registers occupied, the whole aggregate spills and the following
	// integer still uses the unconsumed first GPR.
	IntDouble exhausted_value{10, 32.0};
	if (external_int_double_after_8_doubles(
			1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, exhausted_value, 5) != 83) {
		return 35;
	}
	if (external_call_flashcpp_int_double_after_8_doubles(
			1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, exhausted_value, 5) != 83) {
		return 36;
	}

	return 0; // All tests passed!
}
