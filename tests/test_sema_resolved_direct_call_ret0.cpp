// Test: sema-resolved direct call target for free functions
// This exercises Phase 1 of the codegen lookup cleanup.
// Free function calls should be resolved by sema and consumed
// by codegen without falling through to name-based recovery chains.

int add(int a, int b) {
	return a + b;
}

int multiply(int a, int b) {
	return a * b;
}

int negate(int x) {
	return -x;
}

int main() {
	// Simple free function calls - should use sema-resolved target
	if (add(10, 32) != 42)
		return 1;
	if (multiply(6, 7) != 42)
		return 2;
	if (negate(-42) != 42)
		return 3;

	// Nested free function calls
	if (add(multiply(5, 8), 2) != 42)
		return 4;
	if (negate(add(-40, -2)) != 42)
		return 5;

	return 0;
}
