// Phase 9: Global/static compound assignment with cross-type operands.
// C++20 [expr.ass]/7: E1 op= E2 behaves as E1 = static_cast<T1>(E1 op E2).
// The binary operation uses usual arithmetic conversions; the result converts
// back to the LHS type.

double g_double = 10.0;
int g_int = 3;

int main() {
	int result = 0;

	// double += int: 10.0 + 5 = 15.0 (common type: double)
	g_double += 5;
	if (g_double > 14.5 && g_double < 15.5) {
		result += 1;
	}

	// int *= double: (int)(3.0 * 1.5) = (int)4.5 = 4 (common type: double)
	g_int *= 1.5;
	if (g_int == 4) {
		result += 10;
	}

	// double -= int: 15.0 - 3 = 12.0
	g_double -= 3;
	if (g_double > 11.5 && g_double < 12.5) {
		result += 100;
	}

	// Expected: 111 (1 + 10 + 100)
	return result - 111;
}
