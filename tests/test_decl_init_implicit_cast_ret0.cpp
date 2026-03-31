// Test: float->int and int->float implicit conversions in variable initializers (Phase 3).
// Previously broken: int i = double_var; stored raw float bits instead of converting.
// The fix adds primitive-to-primitive conversion in visitVariableDeclarationNode.

int main() {
	double d = 3.7;
	int i1 = d;			// double -> int truncation: expected 3

	float f = 2.9f;
	int i2 = f;			// float -> int truncation: expected 2

	int i3 = 42;
	double d2 = i3;		// int -> double: expected 42.0
	float f2 = i3;	   // int -> float: expected 42.0f

	// i1=3, i2=2, d2=42.0, f2=42.0
	// (3-3) + (2-2) + (int)(42.0-42.0) + (int)(42.0f-42.0f) = 0
	return (i1 - 3) + (i2 - 2) + (int)(d2 - 42.0) + (int)(f2 - 42.0f);
}
