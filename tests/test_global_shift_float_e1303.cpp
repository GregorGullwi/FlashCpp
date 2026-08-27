// Phase 9: Compound shift assignment with a floating-point RHS is ill-formed
// per C++20 [expr.shift]/1 which requires integral or unscoped enum operands.
// FlashCpp should emit diagnostic 1303.

int g = 8;
double d = 3.0;

int main() {
	g >>= d;	 // ill-formed: floating-point shift count
	return g;
}
