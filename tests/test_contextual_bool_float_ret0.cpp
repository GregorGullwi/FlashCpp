// Contextual bool conversion: float/double → bool in control-flow conditions
// C++20 [conv.bool]: zero value → false, nonzero → true

int main() {
	double d = 3.14;
	double dzero = 0.0;
	float f = 1.5f;
	float fzero = 0.0f;

	// if: nonzero double is true
	if (d) {
		// ok
	} else {
		return 1;
	}

	// if: zero double is false
	if (dzero) {
		return 2;
	}

	// if: nonzero float is true
	if (f) {
		// ok
	} else {
		return 3;
	}

	// if: zero float is false
	if (fzero) {
		return 4;
	}

	// ternary with float condition
	int result = f ? 10 : 20;
	if (result != 10) {
		return 5;
	}
	int result2 = fzero ? 10 : 20;
	if (result2 != 20) {
		return 6;
	}

	// while with double condition
	double countdown = 2.0;
	int loops = 0;
	while (countdown) {
		loops = loops + 1;
		if (loops >= 3) {
			countdown = 0.0;
		}
	}
	if (loops != 3) {
		return 7;
	}

	return 0;
}
