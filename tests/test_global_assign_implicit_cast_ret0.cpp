// Phase 9: Global variable assignment with implicit type conversion.
// C++20 [expr.ass]: the RHS is implicitly converted to the type of the LHS.
// Tests int→double, double→int, and char→int conversions for global assignments.

double g_double = 0.0;
int g_int = 0;
char g_char = 0;

int main() {
	int result = 0;

 // int → double: 42 should become 42.0
	g_double = 42;
	if (g_double > 41.5) {
		result += 1;
	}

 // double → int: 3.7 should truncate to 3
	g_int = 3.7;
	if (g_int == 3) {
		result += 10;
	}

 // int → char: 65 fits in char
	g_char = 65;
	if (g_char == 65) {
		result += 100;
	}

 // Expected: 111 (1 + 10 + 100)
	return result - 111;
}
