// C++20 [expr.bit.and]: bitwise operators require integral operands.
// int &= double is ill-formed because the common type is double (floating-point).
// The compiler should reject this instead of silently emitting wrong code.
int main() {
	int x = 7;
	double d = 3.0;
	x &= d;	// ill-formed: bitwise AND not defined for floating-point operands
	return 0;
}
