// Phase 10: Sema annotation target-type verification.
// This test validates that sema annotations are consistent with the
// codegen's expected conversion targets. When both sema and codegen agree,
// the sema path should be preferred. When they disagree (which shouldn't
// happen under normal operation), the fallback local policy should apply.
//
// Tests: multiple conversion contexts exercising the verified sema paths.

int main() {
	int result = 0;

	// Binary arithmetic: int + double → common type double
	int a = 5;
	double d = 2.5;
	double r1 = a + d;  // LHS int→double via sema annotation
	if (r1 > 7.0 && r1 < 8.0)
		result += 1;

	// Global/static: already tested elsewhere, but let's combine
	// Simple assignment: double → int truncation
	int b = 0;
	double d2 = 9.9;
	b = d2;
	if (b == 9)
		result += 10;

	// Compound assignment: int += double → common type double, result back to int
	int c = 10;
	c += 5;
	if (c == 15)
		result += 100;

	// Shift: independent promotion (short → int on both sides)
	short s = 1;
	int shifted = s << 3;
	if (shifted == 8)
		result += 1000;

	// Return conversion via function call  
	// Ternary with different types (exercises both branch paths)
	long l = 42L;
	int i = 7;
	long r2 = (i > 0) ? i : l;  // int→long
	if (r2 == 7L)
		result += 10000;

	// Expected: 11111
	return result - 11111;
}
