// Regression test: fractional float values must be truthy in conditions.
// C++20 [conv.bool]: a zero value is converted to false; any other value is true.
// Values like 0.5, 0.1f, 0.99 must NOT be truncated to 0 by cvttsd2si/cvttss2si.

int main() {
	// Double fractional values — all truthy
	double d1 = 0.5;
	if (d1) { /* ok */ } else { return 1; }

	double d2 = 0.1;
	if (d2) { /* ok */ } else { return 2; }

	double d3 = 0.99;
	if (d3) { /* ok */ } else { return 3; }

	// Float fractional values — all truthy
	float f1 = 0.5f;
	if (f1) { /* ok */ } else { return 4; }

	float f2 = 0.1f;
	if (f2) { /* ok */ } else { return 5; }

	float f3 = 0.99f;
	if (f3) { /* ok */ } else { return 6; }

	// Logical operators with fractional floats
	double d4 = 0.5;
	int x = 1;
	if (d4 && x) { /* ok */ } else { return 7; }
	if (d4 || x) { /* ok */ } else { return 8; }

	// NOT operator with fractional float
	double d5 = 0.5;
	if (!d5) { return 9; }

	// While loop with fractional float (should enter once)
	float f4 = 0.5f;
	int count = 0;
	while (f4) {
		count++;
		f4 = 0.0f;
	}
	if (count != 1) { return 10; }

	// Zero values must still be falsy
	double zero = 0.0;
	if (zero) { return 11; }

	float fzero = 0.0f;
	if (fzero) { return 12; }

	// Negative zero must also be falsy
	double neg_zero = -0.0;
	if (neg_zero) { return 13; }

	return 0;
}
