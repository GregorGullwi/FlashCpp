// Test: pack expansion with function-call wrapping each element
// Verifies that non-trivial expressions in ... are correctly expanded

int clamp_pos(int x) { return x < 0 ? 0 : x; }
int add3(int a, int b, int c) { return a + b + c; }

template <typename... Args>
int sum_clamped(Args... args) {
	return add3(clamp_pos(args)...);
}

int main() {
	// clamp_pos(10) + clamp_pos(-5) + clamp_pos(37) = 10 + 0 + 32
	return sum_clamped(10, -5, 32);	// = 42
}
