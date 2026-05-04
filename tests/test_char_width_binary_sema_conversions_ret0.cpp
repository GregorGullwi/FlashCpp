#include <climits>

// C++20 usual arithmetic conversions for wide character types must be
// annotated by semantic analysis even inside constexpr-style helper returns.

struct WideLimitsLike {
	static wchar_t min() {
		return ((wchar_t)(-1) < 0) ? wchar_t(-2147483647 - 1) : (wchar_t)0;
	}

	static char32_t max() {
		int bits = sizeof(char32_t) * CHAR_BIT;
		int value_bits = bits - ((char32_t)(-1) < 0);
		char32_t high_bit = (char32_t)1 << (value_bits - 1);
		char32_t all_lower_bits = high_bit - 1;
		return ((char32_t)(-1) < 0)
				   ? char32_t(-2147483647 - 1)
				   : ((all_lower_bits << 1) + 1);
	}
};

int main() {
	wchar_t w = 1;
	int promoted = w + 2;

	char32_t c32 = 1;
	char32_t widened = 2 + c32;

	if (promoted != 3)
		return 1;
	if (widened != 3)
		return 2;

	(void)WideLimitsLike::min();
	(void)WideLimitsLike::max();
	return 0;
}
