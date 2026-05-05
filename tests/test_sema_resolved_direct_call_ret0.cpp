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

template <typename T>
int addTwo(T value) {
	return static_cast<int>(value) + 2;
}

namespace math {
	template <typename T>
	int multiplyBySix(T value) {
		return static_cast<int>(value) * 6;
	}
}

int callAdd(int value) {
	return addTwo<int>(value);
}

int callMultiply(int value) {
	return math::multiplyBySix<int>(value);
}

int finalizeValue(int value) {
	return value + 7;
}

template <typename T>
struct Runner {
	int run(T value) {
		return finalizeValue(static_cast<int>(value));
	}
};

struct Calculator {
	int base;

	int doubleBase() {
		return base * 2;
	}

	int addToBase(int x) {
		return base + x;
	}

	int compute() {
		return addToBase(doubleBase());
	}
};

int main() {
	if (add(10, 32) != 42)
		return 1;
	if (multiply(6, 7) != 42)
		return 2;
	if (negate(-42) != 42)
		return 3;

	if (add(multiply(5, 8), 2) != 42)
		return 4;
	if (negate(add(-40, -2)) != 42)
		return 5;

	if (addTwo<int>(40) != 42)
		return 6;
	if (math::multiplyBySix<int>(7) != 42)
		return 7;
	if (callAdd(40) != 42)
		return 8;
	if (callMultiply(7) != 42)
		return 9;

	Runner<int> int_runner;
	if (int_runner.run(35) != 42)
		return 10;
	Runner<long> long_runner;
	if (long_runner.run(35) != 42)
		return 11;

	Calculator c;
	c.base = 10;
	if (c.doubleBase() != 20)
		return 12;
	if (c.compute() != 30)
		return 13;
	if (c.addToBase(32) != 42)
		return 14;

	return 0;
}
