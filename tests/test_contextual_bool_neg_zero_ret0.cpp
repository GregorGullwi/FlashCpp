// Contextual bool conversion: negative zero (-0.0) edge case
// C++20 [conv.bool]: -0.0 is semantically zero, should convert to false
// This is a regression test for the float→bool condition conversion.

int main() {
	double neg_zero_d = -0.0;
	float neg_zero_f = -0.0f;

 // if: -0.0 double is false
	if (neg_zero_d) {
		return 1;
	}

 // if: -0.0 float is false
	if (neg_zero_f) {
		return 2;
	}

 // ternary: -0.0 is false
	int result = neg_zero_d ? 10 : 20;
	if (result != 20) {
		return 3;
	}

 // while: -0.0 is false (loop should not execute)
	int loops = 0;
	while (neg_zero_f) {
		loops = loops + 1;
		break;
	}
	if (loops != 0) {
		return 4;
	}

	return 0;
}
