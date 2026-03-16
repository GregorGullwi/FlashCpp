// Contextual bool conversion: ! (logical NOT) with float/double operands
// C++20 [expr.unary.op]/9: the operand of ! is contextually converted to bool
// Regression test for -0.0 handling in the ! operator.

int main() {
	double d = -0.0;
	float f = -0.0f;

	// !(-0.0) should be true: -0.0 is semantically zero → false → !false = true
	if (!d) {
		// ok
	} else {
		return 1;
	}

	if (!f) {
		// ok
	} else {
		return 2;
	}

	// nonzero float: !(3.14) should be false
	double nonzero = 3.14;
	if (!nonzero) {
		return 3;
	}

	// !(0.0) should be true
	double zero = 0.0;
	if (!zero) {
		// ok
	} else {
		return 4;
	}

	return 0;
}
