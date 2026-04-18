// Expected to fail: hex floating-point literal without mandatory p/P exponent.
//
// Per C++20 [lex.fcon], a hexadecimal floating-point literal REQUIRES a
// binary-exponent-part (p or P).  The token "0x1.0" without a trailing
// 'p' exponent is ill-formed.
//
// With a correct lexer, "0x1.0" is tokenized as the hex integer "0x1"
// followed by the decimal float ".0", which makes the declaration below
// a parse error (two consecutive literals with no operator).
//
// A buggy lexer that greedily consumes '.' into the hex literal would
// treat "0x1.0" as a single (malformed) numeric token and might silently
// accept this code.

int main() {
	double d = 0x1.0;  // ill-formed: missing p/P binary exponent
	return 0;
}
