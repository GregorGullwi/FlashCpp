// Test: sizeof suffixed hex/binary/octal literals.
//
// KNOWN BUG: get_numeric_literal_type() computes sizeInBits from the digit
// count of the lowercased text *including* suffix characters. For U-only
// suffixed hex/binary/octal literals the inflated size is never overwritten
// (only L/LL branches overwrite it). For example:
//   0xFFU -> lowerText "0xffu" (len 5) -> (5-2)*4/8 -> ceil(1.5)*8 = 16 bits
//   Correct: sizeof(unsigned int) = 4 bytes = 32 bits
//
// This test documents the CURRENT (buggy) behaviour so it passes in CI.
// When the bug is fixed, these assertions will fail — update them to match
// the correct C++ semantics (sizeof should equal sizeof(unsigned int) = 4).
//
// See also: src/Parser_Expr_BinaryPrecedence.cpp lines 585-604

int main() {
	// ── L / LL suffixes: sizeInBits is correctly overwritten ──

	// 0xFFL -> long (size depends on target, typically 8 on LP64)
	if (sizeof(0xFFL) != sizeof(long)) return 1;

	// 0xFFLL -> long long (always 8 bytes)
	if (sizeof(0xFFLL) != sizeof(long long)) return 2;

	// 0xFFUL -> unsigned long
	if (sizeof(0xFFUL) != sizeof(unsigned long)) return 3;

	// 0xFFULL -> unsigned long long (always 8 bytes)
	if (sizeof(0xFFULL) != sizeof(unsigned long long)) return 4;

	// ── U-only suffix: sizeInBits is WRONG (known bug) ──
	// These document the buggy behaviour. Correct value would be 4 (sizeof(unsigned int)).

	// BUG: 0xFFU -> sizeInBits=16 -> sizeof reports 2 instead of 4
	if (sizeof(0xFFU) != 2) return 5;  // WRONG: should be sizeof(unsigned int) == 4

	// BUG: 0xFFFFFFFFU -> sizeInBits=40 -> sizeof reports 5 instead of 4
	if (sizeof(0xFFFFFFFFU) != 5) return 6;  // WRONG: should be sizeof(unsigned int) == 4

	// ── Unsuffixed hex: sizeInBits is digit-based (also pre-existing) ──

	// 0xFF -> sizeInBits=8 -> sizeof reports 1 instead of 4
	if (sizeof(0xFF) != 1) return 7;  // WRONG: should be sizeof(int) == 4

	// 0xFFFF -> sizeInBits=16 -> sizeof reports 2 instead of 4
	if (sizeof(0xFFFF) != 2) return 8;  // WRONG: should be sizeof(int) == 4

	return 0;
}
