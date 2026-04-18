// Test valid hex floating-point literals (with mandatory p/P exponent).
//
// Per C++20 [lex.fcon], a hexadecimal floating-point literal requires a
// binary-exponent-part (p/P).  This test verifies the lexer correctly
// tokenizes valid hex float forms.

int main() {
	// Hex float with fractional part and p exponent: 0x1.0 * 2^4 = 16.0
	double hex_float = 0x1.0p+4;
	if (hex_float < 15.9 || hex_float > 16.1) return 1;

	// Hex float without fractional part but with p exponent: 1 * 2^10 = 1024.0
	double hex_float2 = 0x1p10;
	if (hex_float2 < 1023.9 || hex_float2 > 1024.1) return 2;

	// Hex float with negative exponent: 0x1.0 * 2^-3 = 0.125
	double hex_float3 = 0x1.0p-3;
	if (hex_float3 < 0.124 || hex_float3 > 0.126) return 3;

	return 0;
}
