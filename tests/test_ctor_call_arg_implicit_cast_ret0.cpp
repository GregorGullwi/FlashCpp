// Phase 8a: Constructor call arguments should receive implicit conversions
// from the semantic analysis pass, just like regular function call arguments.
// This test verifies that passing an int where a double is expected (and vice
// versa) inside a constructor call produces the correct result.

struct Pair {
	double a;
	int b;
	Pair(double x, int y) : a(x), b(y) {}
};

int main() {
 // Pass int literal where double is expected, and double where int is expected.
	Pair p(42, 3.14);

 // p.a should be 42.0 (int→double), p.b should be 3 (double→int truncation).
	int result = 0;
	if (p.a < 41.5 || p.a > 42.5)
		result = 1;	// a should be ~42.0
	if (p.b != 3)
		result = 2;					 // b should be 3

	return result;  // 0 on success
}
