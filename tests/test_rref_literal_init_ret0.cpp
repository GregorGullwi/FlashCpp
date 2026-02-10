// Test rvalue reference and const lvalue reference initialization from literals.
// C++ allows binding rvalue references and const lvalue references to literals
// by materializing a temporary with extended lifetime.

int main() {
	// Rvalue reference bound to integer literal
	int&& rr = 42;
	if (rr != 42) return 1;

	// Const lvalue reference bound to integer literal
	const int& cr = 100;
	if (cr != 100) return 2;

	// Rvalue reference bound to zero
	int&& rr_zero = 0;
	if (rr_zero != 0) return 3;

	// Rvalue reference bound to negative literal
	int&& rr_neg = -7;
	if (rr_neg != -7) return 4;

	return 0;
}
