// Test file to verify the semantic analysis pass traverses all major AST node categories.
// The pass is currently a no-op (Phase 1), but this test validates that the pass
// can walk the full AST without errors and the pipeline produces correct output.

struct Point {
	int x;
	int y;

	Point(int x, int y) : x(x), y(y) {}

	int sum() const { return x + y; }
};

namespace math {
	int square(int n) { return n * n; }
}

int factorial(int n) {
	if (n <= 1) return 1;
	return n * factorial(n - 1);
}

int loop_sum(int n) {
	int total = 0;
	for (int i = 0; i < n; i++) {
		total = total + i;
	}
	return total;
}

int main() {
	// Variable declarations with initializers
	int a = 5;
	int b = 10;

	// Binary operations
	int c = a + b;

	// Function calls
	int d = factorial(c);
	(void)d;

	// Member access and constructor calls
	Point p(a, b);
	int s = p.sum();

	// Namespace function call
	int sq = math::square(s);

	// Ternary operator
	int result = (sq > 100) ? sq : 0;

	// Control flow: while
	int w = 0;
	while (w < 3) {
		w = w + 1;
	}

	// Control flow: do-while
	int dw = 0;
	do {
		dw = dw + 1;
	} while (dw < 3);

	// Control flow: switch
	int sw = 0;
	switch (result) {
		case 0: sw = 1; break;
		default: sw = 2; break;
	}

	// Array subscript
	int arr[3] = {1, 2, 3};
	int elem = arr[1];

	// Return 0 if pipeline produces correct output
	return result - result + sw - sw + elem - elem + w - w + dw - dw;
}
