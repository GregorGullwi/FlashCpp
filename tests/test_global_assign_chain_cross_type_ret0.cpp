// Phase 9 regression: global assignment expression result used as subexpression.
// Per C++20 [expr.ass]/3, the result of (g_double = 42) is an lvalue of type
// double with value 42.0.  When that result feeds into another expression the
// value and type must be correct.
//
// The current codegen returns the converted RHS temporary, which has the right
// *type* but is a prvalue, not an lvalue.  This test checks the observable
// value; if the conversion were missing or the wrong temporary were returned
// the arithmetic would produce a wrong result.

double g_double = 0.0;
int g_int = 0;

int main() {
	int result = 0;

	// Use the result of a cross-type global assignment in an expression.
	// (g_double = 42) should yield 42.0 (double), then + 0.5 = 42.5,
	// then int x should truncate to 42.
	int x = (g_double = 42) + 0.5;
	if (x == 42) {
		result += 1;
	}

	// Chain two global assignments with different types.
	// g_int = (g_double = 3.7) should:
	//   1. Store 3.7 into g_double  (g_double == 3.7)
	//   2. Return 3.7 (double)
	//   3. Truncate to int 3 and store into g_int
	g_int = (g_double = 3.7);
	if (g_int == 3) {
		result += 10;
	}
	// g_double must still be 3.7 (not corrupted by the outer assignment)
	if (g_double > 3.5 && g_double < 3.9) {
		result += 100;
	}

	// Expected: 111 (1 + 10 + 100)
	return result - 111;
}
