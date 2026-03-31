// Phase 9: Static local variable assignment with implicit type conversion.
// C++20 [expr.ass]: the RHS is implicitly converted to the type of the LHS.
// Static locals use the same GlobalStore path as globals.

int main() {
	static double s_double = 0.0;
	static int s_int = 0;
	int result = 0;

 // int → double
	s_double = 100;
	if (s_double > 99.5) {
		result += 1;
	}

 // double → int (truncation)
	s_int = 7.9;
	if (s_int == 7) {
		result += 10;
	}

 // Expected: 11 (1 + 10)
	return result - 11;
}
