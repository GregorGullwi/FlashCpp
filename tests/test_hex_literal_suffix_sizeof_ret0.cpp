// Test: sizeof suffixed hex/binary/octal literals should match the type size,
// not the digit count. This is a pre-existing issue where the initial sizeInBits
// calculation in get_numeric_literal_type() counts suffix characters ('u') as
// digits, inflating the computed size. For L/LL suffixes the size is overwritten
// correctly, but for U-only suffixes the inflated value persists.
//
// Per C++20 [lex.icon], 0xFFU is unsigned int (4 bytes), not 2 bytes.

int main() {
	// 0xFFU should be unsigned int (4 bytes), not 2 bytes
	if (sizeof(0xFFU) != sizeof(unsigned int)) return 1;

	// 0xFFFFFFFFU should be unsigned int (4 bytes), not 5 bytes
	if (sizeof(0xFFFFFFFFU) != sizeof(unsigned int)) return 2;

	// 0b1010U should be unsigned int (4 bytes), not 1 byte
	if (sizeof(0b1010U) != sizeof(unsigned int)) return 3;

	// 010U (octal) should be unsigned int (4 bytes)
	if (sizeof(010U) != sizeof(unsigned int)) return 4;

	// Unsuffixed hex should also be int-sized (4 bytes)
	// 0xFF is int per C++ standard, sizeof should be 4
	if (sizeof(0xFF) != sizeof(int)) return 5;

	// L suffix should give long-sized result
	if (sizeof(0xFFL) != sizeof(long)) return 6;

	// LL suffix should give long long-sized result (8 bytes)
	if (sizeof(0xFFLL) != sizeof(long long)) return 7;

	// UL suffix should give unsigned long-sized result
	if (sizeof(0xFFUL) != sizeof(unsigned long)) return 8;

	return 0;
}
