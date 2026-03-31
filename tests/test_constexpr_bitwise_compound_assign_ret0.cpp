// Test: bitwise compound assignment operators in constexpr function bodies
// Tests: &=, |=, ^=, <<=, >>=

constexpr int test_bitwise_and_assign() {
	int x = 0xFF;
	x &= 0x0F;
	return x;  // 0xFF & 0x0F = 0x0F = 15
}
static_assert(test_bitwise_and_assign() == 15);

constexpr int test_bitwise_or_assign() {
	int x = 0x0F;
	x |= 0xF0;
	return x;  // 0x0F | 0xF0 = 0xFF = 255
}
static_assert(test_bitwise_or_assign() == 255);

constexpr int test_bitwise_xor_assign() {
	int x = 0xFF;
	x ^= 0x0F;
	return x;  // 0xFF ^ 0x0F = 0xF0 = 240
}
static_assert(test_bitwise_xor_assign() == 240);

constexpr int test_shl_assign() {
	int x = 1;
	x <<= 4;
	return x;  // 1 << 4 = 16
}
static_assert(test_shl_assign() == 16);

constexpr int test_shr_assign() {
	int x = 256;
	x >>= 3;
	return x;  // 256 >> 3 = 32
}
static_assert(test_shr_assign() == 32);

// Mix compound assignments in a loop
constexpr int bit_accumulate() {
	int flags = 0;
	for (int i = 0; i < 4; ++i) {
		flags |= (1 << i);  // set bits 0..3
	}
	flags &= 0b1010;	 // keep only bits 1 and 3  → 0b1010 = 10
	return flags;
}
static_assert(bit_accumulate() == 10);

// XOR-swap style
constexpr int xor_swap() {
	int a = 7, b = 13;
	a ^= b;
	b ^= a;
	a ^= b;
	// a and b are now swapped
	return a * 100 + b;	// 13 * 100 + 7 = 1307
}
static_assert(xor_swap() == 1307);

// Shift-assign in a loop (shift right repeatedly)
constexpr int count_bits(int n) {
	int count = 0;
	while (n != 0) {
		count += n & 1;
		n >>= 1;
	}
	return count;
}
static_assert(count_bits(0b10110101) == 5);
static_assert(count_bits(0xFF) == 8);
static_assert(count_bits(0) == 0);

// unsigned types
constexpr unsigned int test_unsigned_bitops() {
	unsigned int x = 0xFFFFFFFFu;
	x &= 0x00FF00FFu;
	x ^= 0x00AA00AAu;
	return x;  // (0xFFFFFFFF & 0x00FF00FF) ^ 0x00AA00AA = 0x00FF00FF ^ 0x00AA00AA = 0x00550055
}
static_assert(test_unsigned_bitops() == 0x00550055u);

int main() { return 0; }
