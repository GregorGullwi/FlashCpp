// Test: non-constexpr (runtime) variable assigned from constexpr function call
//
// C++ standard: constexpr functions are called at runtime when assigned to
// non-constexpr variables. The function body must still execute and produce
// the correct result.

constexpr int square(int x) {
	return x * x;
}

constexpr int cube(int x) {
	return x * x * x;
}

constexpr int add(int a, int b) {
	return a + b;
}

// This global constexpr IS evaluated at compile time (stored in .rodata)
constexpr int compile_time_val = square(5);	// = 25
static_assert(compile_time_val == 25, "Compile-time evaluation works");

int main() {
	// Case 1: int (non-constexpr) assigned from constexpr function with constant args
	// The function is called at runtime (since x is not constexpr)
	int x = square(5);
	if (x != 25)
		return 1;

	// Case 2: int assigned from constexpr function with runtime variable arg
	int n = 7;
	int y = square(n);
	if (y != 49)
		return 2;

	// Case 3: arithmetic with multiple constexpr function calls
	int z = square(3) + square(4);  // 9 + 16 = 25
	if (z != 25)
		return 3;

	// Case 4: nested constexpr function calls into non-constexpr variable
	int w = cube(square(2));	 // square(2)=4, cube(4)=64
	if (w != 64)
		return 4;

	// Case 5: mixing compile-time and runtime
	int mixed = add(compile_time_val, square(3));  // 25 + 9 = 34
	if (mixed != 34)
		return 5;

	return 0;
}
