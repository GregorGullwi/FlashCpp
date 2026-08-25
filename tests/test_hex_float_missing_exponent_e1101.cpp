// Reduced regression for DiagnosticId::HexFloatRequiresBinaryExponent (1101).
// Per C++20 [lex.fcon], a hexadecimal floating-point literal with a
// fractional part requires a binary-exponent-part (p or P). The ill-formed
// literal must be diagnosed instead of being split into unrelated tokens.
int main() {
	double d = 0x1.0;  // ill-formed: missing p/P binary exponent
	return 0;
}
