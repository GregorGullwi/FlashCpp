// Test: implicit argument type conversions in function calls (Phase 2 sema migration).
// The semantic pass annotates argument expressions; AstToIr reads the annotations.

int sum_as_long(long a, long b) { return (int)(a + b); }

int take_double_and_convert(double a) { return (int)(a + 0.5); }

int main() {
 // int → long argument conversions
	int x = 3, y = 4;
	int r1 = sum_as_long(x, y);			// both int → long, result 7

 // int literal → double argument conversion
	int r2 = take_double_and_convert(5); // 5 (int) → 5.0 (double), then 5.0 + 0.5 = 5.5 → 5 (int)

	return (r1 - 7) + (r2 - 5);
}
