// Regression test for float/double array subscript reads.
// Before the fix, darr[0] would read 0 instead of 1.0, so the function
// would return 0 instead of 42.
int main() {
	// double array: constant-index load
	double darr[3] = { 1.0, 2.0, 3.0 };
	double x = darr[0];
	if (x < 0.5) return 1;  // wrong: darr[0] should be 1.0

	// double array: verify all three elements
	double b = darr[1];
	if (b < 1.5) return 2;  // wrong: darr[1] should be 2.0

	double c = darr[2];
	if (c < 2.5) return 3;  // wrong: darr[2] should be 3.0

	// float array: constant-index load
	float farr[2] = { 1.5f, 2.5f };
	float y = farr[0];
	if (y < 1.0f) return 4;  // wrong: farr[0] should be 1.5

	float z = farr[1];
	if (z < 2.0f) return 5;  // wrong: farr[1] should be 2.5

	// double array: variable-index load
	int idx = 2;
	double d = darr[idx];
	if (d < 2.5) return 6;  // wrong: darr[2] should be 3.0

	return 42;
}
