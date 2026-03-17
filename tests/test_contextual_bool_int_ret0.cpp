// Contextual bool conversion: int → bool in control-flow conditions
// C++20 [stmt.select]: condition is contextually converted to bool

int main() {
	int x = 42;
	int zero = 0;

	// if: nonzero int is true
	if (x) {
		// ok
	} else {
		return 1;
	}

	// if: zero int is false
	if (zero) {
		return 2;
	}

	// while: nonzero int → true, zero → false
	int count = 3;
	while (count) {
		count = count - 1;
	}
	if (count) {
		return 3;
	}

	// for: int condition
	int sum = 0;
	for (int i = 5; i; i = i - 1) {
		sum = sum + 1;
	}
	if (sum != 5) {
		return 4;
	}

	// do-while: int condition
	int n = 3;
	do {
		n = n - 1;
	} while (n);
	if (n) {
		return 5;
	}

	// ternary: int condition
	int result = x ? 10 : 20;
	if (result != 10) {
		return 6;
	}
	int result2 = zero ? 10 : 20;
	if (result2 != 20) {
		return 7;
	}

	return 0;
}
