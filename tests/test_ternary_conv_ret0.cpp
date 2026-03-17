// Test: ternary operator branch type conversions (Phase 7).
// C++20 [expr.cond]: if the second and third operands have different
// types, they are converted to a common type via usual arithmetic conversions.

int main() {
	int a = 5;
	long b = 10L;

	// int and long branches -> common type is long
	long r1 = (a > 3) ? a : b;
	long r2 = r1 - 5L;  // should be 0 (condition true, a=5 promoted to long)

	long r3 = (a > 100) ? a : b;
	long r4 = r3 - 10L;  // should be 0 (condition false, b=10)

	// int and double branches -> common type is double
	double d = 3.5;
	double r5 = (a > 0) ? a : d;
	int r6 = (int)(r5 - 5.0);  // should be 0 (condition true, a=5 promoted to double)

	double r7 = (a < 0) ? a : d;
	int r8 = (int)((r7 - 3.5) * 10.0);  // should be 0 (condition false, d=3.5)

	return (int)r2 + (int)r4 + r6 + r8;
}
