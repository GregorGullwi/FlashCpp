// Test: constexpr ++/-- operators wrap unsigned types at their declared width.
// Regression test for the bug where EvalResult::from_int(1) lacked exact_type,
// preventing apply_binary_op from masking the result to the operand's width.

#include <climits>

// --- unsigned int (32-bit) ---

constexpr unsigned int uint_prefix_inc_wrap() {
	unsigned int x = UINT_MAX;
	++x;
	return x;
}

constexpr unsigned int uint_postfix_inc_wrap() {
	unsigned int x = UINT_MAX;
	x++;
	return x;
}

constexpr unsigned int uint_prefix_dec_wrap() {
	unsigned int x = 0u;
	--x;
	return x;
}

constexpr unsigned int uint_postfix_dec_wrap() {
	unsigned int x = 0u;
	x--;
	return x;
}

// --- unsigned char (8-bit) ---

constexpr unsigned char uchar_inc_wrap() {
	unsigned char x = 255;
	++x;
	return x;
}

constexpr unsigned char uchar_dec_wrap() {
	unsigned char x = 0;
	--x;
	return x;
}

// --- unsigned short (16-bit) ---

constexpr unsigned short ushort_inc_wrap() {
	unsigned short x = 65535;
	++x;
	return x;
}

constexpr unsigned short ushort_dec_wrap() {
	unsigned short x = 0;
	--x;
	return x;
}

static_assert(uint_prefix_inc_wrap()  == 0u);
static_assert(uint_postfix_inc_wrap() == 0u);
static_assert(uint_prefix_dec_wrap()  == UINT_MAX);
static_assert(uint_postfix_dec_wrap() == UINT_MAX);

static_assert(uchar_inc_wrap() == 0);
static_assert(uchar_dec_wrap() == 255);

static_assert(ushort_inc_wrap() == 0);
static_assert(ushort_dec_wrap() == 65535);

int main() {
	return 0;
}
