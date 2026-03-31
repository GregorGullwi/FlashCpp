// Test: compound assignment implicit conversions via sema annotation (Phase 7).
// Validates that compound assignment operators (+=, -=, *=, /=)
// correctly convert the RHS to the LHS type before operating.

int main() {
 // long += int: int RHS should be promoted to long
	long x = 10L;
	int a = 5;
	x += a;
	long r1 = x - 15L;  // should be 0

 // double += int: int RHS should be converted to double
	double d = 1.5;
	d += a;
	int r2 = (int)(d - 6.5);	 // should be 0

 // int -= double: truncation of RHS before subtraction
 // C++ compound assignment: i -= d means i = i - (int)d... no, actually
 // C++ says i -= d means i = static_cast<int>(i - d) but with usual
 // arithmetic conversion on the operands first.
 // So: i=10, d=3.5: i -= d => i = (int)(10.0 - 3.5) = (int)6.5 = 6
	int i = 10;
	double d2 = 3.5;
	i -= d2;
	int r3 = i - 6;	// should be 0

 // int *= int: no conversion needed
	int j = 7;
	j *= 3;
	int r4 = j - 21;	 // should be 0

	return (int)r1 + r2 + r3 + r4;
}
