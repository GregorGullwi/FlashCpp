// Contextual bool conversion: logical operators (&&, ||, !) with non-bool operands
// C++20 [expr.log.and], [expr.log.or], [expr.unary.op]:
// operands are contextually converted to bool

int main() {
	int a = 5;
	int b = 0;
	int c = 3;

 // && with int operands
	if (a && c) {
	// both nonzero → true
	} else {
		return 1;
	}

	if (a && b) {
		return 2; // b is zero → false
	}

 // || with int operands
	if (a || b) {
	// a nonzero → true
	} else {
		return 3;
	}

	if (b || b) {
		return 4; // both zero → false
	}

 // ! with int operand
	if (!b) {
	// b is zero → !0 is true
	} else {
		return 5;
	}

	if (!a) {
		return 6; // a is nonzero → !nonzero is false
	}

 // Mixed types in logical operators
	double d = 1.5;
	float f = 0.0f;
	if (d && a) {
	// both nonzero → true
	} else {
		return 7;
	}

	if (d && f) {
		return 8; // f is zero → false
	}

 // Nested logical with mixed types
	if ((a || b) && (c || b)) {
	// (5||0)=true && (3||0)=true → true
	} else {
		return 9;
	}

	return 0;
}
