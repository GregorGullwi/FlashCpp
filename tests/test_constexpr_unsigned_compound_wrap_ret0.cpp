// Test: compound assignments apply unsigned type-width truncation in constexpr
// Per C++20, unsigned arithmetic must wrap at the declared type's width.
// e.g. unsigned char x = 200; x += 100; → x == 44  (300 & 0xFF)

// unsigned char: wraps at 8 bits (256)
constexpr unsigned char uchar_add_wrap() {
	unsigned char x = 200;
	x += 100;
	return x;  // 300 & 0xFF = 44
}

constexpr unsigned char uchar_sub_wrap() {
	unsigned char x = 5;
	x -= 10;
	return x;  // (unsigned char)(-5) = 251
}

constexpr unsigned char uchar_mul_wrap() {
	unsigned char x = 200;
	x *= 2;
	return x;  // 400 & 0xFF = 144
}

// unsigned short: wraps at 16 bits (65536)
constexpr unsigned short ushort_add_wrap() {
	unsigned short x = 65000;
	x += 1000;
	return x;  // 66000 & 0xFFFF = 464
}

// unsigned int: wraps at 32 bits
constexpr unsigned int uint_add_wrap() {
	unsigned int x = 4000000000u;
	x += 1000000000u;
	return x;  // 5000000000 & 0xFFFFFFFF = 705032704
}

// Bitwise compound assignments also apply the mask
constexpr unsigned char uchar_or_assign() {
	unsigned char x = 200;
	x |= 100;
	return x;  // 200 | 100 = 236 (fits in byte)
}

constexpr unsigned char uchar_and_assign() {
	unsigned char x = 0xFF;
	x &= 0x0F;
	return x;  // 0xFF & 0x0F = 15
}

constexpr unsigned char uchar_xor_assign() {
	unsigned char x = 0xAA;
	x ^= 0xFF;
	return x;  // 0xAA ^ 0xFF = 0x55 = 85
}

// In a loop: accumulate with wrap
constexpr unsigned char loop_wrap() {
	unsigned char sum = 0;
	for (int i = 0; i < 10; i++) {
		sum += 50;  // each += wraps at 8 bits
	}
	return sum;  // 500 & 0xFF = 244
}

static_assert(uchar_add_wrap() == 44);
static_assert(uchar_sub_wrap() == 251);
static_assert(uchar_mul_wrap() == 144);
static_assert(ushort_add_wrap() == 464);
static_assert(uint_add_wrap() == 705032704u);
static_assert(uchar_or_assign() == 236);
static_assert(uchar_and_assign() == 15);
static_assert(uchar_xor_assign() == 85);
static_assert(loop_wrap() == 244);

int main() {
	return 0;
}
