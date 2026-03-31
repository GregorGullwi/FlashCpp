// Test unsigned compound assignment cross-type: /= and >>= with different-width unsigned types.
// Uses values that would produce wrong results with signed divide/shift (sign-extends incorrectly).
int main() {
	// Cross-type: unsigned int /= unsigned long long → common type is unsigned long long.
	// 0x80000000u = 2147483648; signed interpretation: -2147483648 → idiv would give -1073741824.
	unsigned int x = 0x80000000u;
	unsigned long long big = 2;
	x /= big;
	if (x != 0x40000000u)  // 1073741824
		return 1;

	// Cross-type: unsigned int >>= unsigned long long → common type is unsigned long long.
	// 0x80000000u >> 1 must be 0x40000000u, not 0xC0000000u (arithmetic/signed shift).
	unsigned int y = 0x80000000u;
	unsigned long long shift = 1;
	y >>= shift;
	if (y != 0x40000000u)
		return 2;

	return 0;
}
