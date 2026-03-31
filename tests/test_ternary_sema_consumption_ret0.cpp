// Phase 10: Ternary operator branch sema annotation consumption.
// Validates that the codegen path for ternary ?: branches prefers sema
// annotations over local policy when converting branches to common type.
// Tests: int/double, char/int, int/long ternary branches.

int main() {
	int result = 0;

 // int and double: common type = double
	int a = 10;
	double d = 3.5;
	double r1 = (a > 0) ? a : d;	 // true branch: int→double
	if (r1 > 9.5)
		result += 1;

	double r2 = (a < 0) ? a : d;	 // false branch: d=3.5 (no conv)
	if (r2 > 3.0 && r2 < 4.0)
		result += 10;

 // char and int: common type = int
	char c = 65;
	int b = 200;
	int r3 = (c > 60) ? c : b;	   // true branch: char→int
	if (r3 == 65)
		result += 100;

	int r4 = (c < 0) ? c : b;	  // false branch: b=200
	if (r4 == 200)
		result += 1000;

 // int and long: common type = long
	long l = 100L;
	long r5 = (a > 5) ? a : l;	   // true branch: int→long
	if (r5 == 10L)
		result += 10000;

 // Expected: 11111
	return result - 11111;
}
