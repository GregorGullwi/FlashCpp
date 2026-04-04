// Test: sema-resolved direct call target for same-struct member calls
// Member function calls within the same struct should use the
// sema-resolved target without falling through to recovery chains.

struct Calculator {
	int base;

	int doubleBase() {
		return base * 2;
	}

	int addToBase(int x) {
		return base + x;
	}

	// Calls another member function of the same struct
	int compute() {
		return addToBase(doubleBase());
	}
};

int main() {
	Calculator c;
	c.base = 10;

	// Direct member call
	if (c.doubleBase() != 20)
		return 1;

	// Member calling another member of the same struct
	if (c.compute() != 30)
		return 2;

	if (c.addToBase(32) != 42)
		return 3;

	return 0;
}
