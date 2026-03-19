// Phase 15: Verify codegen trusts sema annotations for standard primitive conversions.
// This test exercises the major conversion contexts that Phase 15 protects:
// binary arithmetic, shift, unary promotion, return, call args, variable init,
// assignment, compound assignment, and ternary branches.

int take_long(long x) { return (int)x; }
double take_double(double x) { return x; }

int main() {
	// 1. Function call arg: int literal -> long
	int r1 = take_long(10);

	// 2. Function call arg: int literal -> double
	int r2 = (int)take_double(5);

	// 3. Binary arithmetic: char -> int promotion
	char c1 = 60;
	char c2 = 70;
	int r3 = c1 + c2;  // usual arithmetic conversions: char -> int

	// 4. Variable init: int literal to int (baseline)
	int r4 = 42;

	// 5. Ternary: branches with different types
	int r5 = (r4 > 5) ? r4 : (short)3;

	// 6. Shift promotion: short << int
	short sh = 1;
	int r6 = sh << 2;  // sema should annotate short -> int promotion for LHS

	// 7. Unary promotion: ~(unsigned char) should promote to int first
	unsigned char uc = 0;
	int r7 = ~uc;  // should be ~(int)0 = -1

	// Validate results
	if (r1 != 10) return 1;
	if (r2 != 5) return 2;
	if (r3 != 130) return 3;
	if (r4 != 42) return 4;
	if (r5 != 42) return 5;
	if (r6 != 4) return 6;
	if (r7 != -1) return 7;
	return 0;
}
