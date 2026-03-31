// Test unsigned modulo: both simple % and cross-type %=
int main() {
	// Simple unsigned % (non-compound) with value > 0x80000000 (would be negative if signed)
	unsigned int x = 0x80000005u;  // 2147483653
	unsigned int y = 4u;
	unsigned int r = x % y;
	if (r != 1u)	 // 2147483653 % 4 = 1
		return 1;

	// Cross-type compound %=: uint32 %= ulonglong
	unsigned int a = 0x80000005u;
	unsigned long long b = 4ull;
	a %= b;
	if (a != 1u)
		return 2;

	return 0;
}
