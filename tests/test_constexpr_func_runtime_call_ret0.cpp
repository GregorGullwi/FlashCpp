// Test: constexpr function actually runs at runtime when called with non-const arguments

constexpr int multiply(int a, int b) {
	return a * b;
}

constexpr int add(int a, int b) {
	return a + b;
}

// This function can be called at both compile time and runtime
constexpr int compute(int x) {
	return x * x + x;
}

int main() {
	// Test 1: constexpr function with compile-time args (can be folded)
	constexpr int compile_time = multiply(3, 4);	 // = 12, evaluated at compile time
	static_assert(compile_time == 12, "Compile-time evaluation works");

	// Test 2: constexpr function with RUNTIME args - must actually run
	int x = 3;  // runtime variable
	int y = 4;  // runtime variable
	int runtime_result = multiply(x, y);	 // = 12, must generate a runtime function call
	if (runtime_result != 12)
		return 1;

	// Test 3: another runtime call
	int a = 5;
	int b = 7;
	int sum = add(a, b);	 // = 12, runtime call
	if (sum != 12)
		return 2;

	// Test 4: compute called with runtime arg
	int n = 6;
	int result = compute(n);	 // = 36 + 6 = 42, runtime call
	if (result != 42)
		return 3;

	return result - 42;	// Should return 0
}
