enum Color { Red = 0,
			 Green = 1,
			 Blue = 2 };

enum Size { Small = 1,
			Medium = 2,
			Large = 3 };

int identity(int x) { return x; }
double toDouble(double d) { return d; }

int returnEnum() {
	return Green;
}

double returnEnumAsDouble() {
	return Medium;
}

constexpr int mockedCharBit = 8;

static wchar_t wideMin() {
	return ((wchar_t)(-1) < 0) ? wchar_t(-2147483647 - 1) : (wchar_t)0;
}

static char32_t char32Max() {
	int bits = sizeof(char32_t) * mockedCharBit;
	int value_bits = bits - ((char32_t)(-1) < 0);
	char32_t high_bit = (char32_t)1 << (value_bits - 1);
	char32_t all_lower_bits = high_bit - 1;
	return ((char32_t)(-1) < 0)
			   ? char32_t(-2147483647 - 1)
			   : ((all_lower_bits << 1) + 1);
}

int main() {
	int r1 = returnEnum();
	if (r1 != 1)
		return 1;

	int r2 = Blue;
	if (r2 != 2)
		return 2;

	int r3;
	r3 = Red;
	if (r3 != 0)
		return 3;

	int r4 = identity(Green);
	if (r4 != 1)
		return 4;

	int r5 = Green + 10;
	if (r5 != 11)
		return 5;

	int r6 = 10 + Blue;
	if (r6 != 12)
		return 6;

	Color c = Green;
	if (c != 1)
		return 7;

	int diff = Blue - Green;
	if (diff != 1)
		return 8;

	double d1 = Medium;
	if (d1 < 1.9 || d1 > 2.1)
		return 9;

	double d2 = toDouble(Large);
	if (d2 < 2.9 || d2 > 3.1)
		return 10;

	double d3 = returnEnumAsDouble();
	if (d3 < 1.9 || d3 > 2.1)
		return 11;

	short a = 5;
	int b = 3;
	int bit_and = a & b;
	int bit_or = a | b;
	int bit_xor = a ^ b;
	if (bit_and + bit_or + bit_xor != 14)
		return 12;

	wchar_t w = 1;
	int promoted = w + 2;
	if (promoted != 3)
		return 13;

	char32_t c32 = 1;
	char32_t widened = 2 + c32;
	if (widened != 3)
		return 14;

	(void)wideMin();
	(void)char32Max();
	return 0;
}
