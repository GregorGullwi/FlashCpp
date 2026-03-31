// Phase 17: Verify compound assignment back-conversion (commonType -> lhsType)
// is annotated by sema. C++20 [expr.ass]/7: E1 op= E2 is equivalent to
// E1 = static_cast<T1>(E1 op E2) where T1 is the type of E1.

int main() {
	short x = 10;
	int y = 5;
	x += y;	// common type is int; result must be back-converted to short
	if (x != 15)
		return 1;

	char c = 'A';
	c += 1;	// common type is int; back-convert to char
	if (c != 'B')
		return 2;

	float f = 1.5f;
	double d = 2.5;
	f += d;	// common type is double; back-convert to float
 // f should be approximately 4.0
	if (f < 3.9f || f > 4.1f)
		return 3;

	return 0;
}
