// Test: unsuffixed integer literals get the correct type based on their value
// per C++20 [lex.icon] Table 8.
//
// Decimal unsuffixed: int → long → long long (first signed type that fits).
// Hex/octal/binary unsuffixed: int → unsigned int → long → unsigned long
//                              → long long → unsigned long long.

int main() {
	// Small hex literal fits in int (32 bits signed)
	if (sizeof(0xFF) != sizeof(int)) return 1;

	// 0x80000000 exceeds signed int max (0x7FFFFFFF) → unsigned int on all platforms
	if (sizeof(0x80000000) != sizeof(unsigned int)) return 2;

	// 0xFFFFFFFF fits in unsigned int (32 bits unsigned)
	if (sizeof(0xFFFFFFFF) != sizeof(unsigned int)) return 3;

	// 0x100000000 exceeds unsigned int → long (LP64) or long long (LLP64)
	// On both platforms, sizeof should be 8 (64-bit type)
	if (sizeof(0x100000000) != 8) return 4;

	// Small decimal literal fits in int
	if (sizeof(42) != sizeof(int)) return 5;

	// Small octal literal fits in int
	if (sizeof(077) != sizeof(int)) return 6;

	// Small binary literal fits in int
	if (sizeof(0b1010) != sizeof(int)) return 7;

	// 0b followed by 32 one-bits = 0xFFFFFFFF → unsigned int (hex/octal/binary rule)
	if (sizeof(0b11111111111111111111111111111111) != sizeof(unsigned int)) return 8;

	return 0;
}
