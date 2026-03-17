// Phase 9: Global/static compound shift assignment uses independent integral
// promotions per C++20 [expr.shift], NOT usual arithmetic conversions.
// The result type is the promoted LHS type; the shift amount undergoes its
// own independent integral promotion and must not be converted to the LHS type.

int g_int = 1;
unsigned int g_uint = 1;
short g_short = 1;
static int s_int = 256;

int main() {
	int result = 0;

	// int <<= int: 1 << 3 = 8
	g_int <<= 3;
	if (g_int == 8) {
		result += 1;
	}

	// unsigned int <<= int: 1 << 4 = 16
	g_uint <<= 4;
	if (g_uint == 16U) {
		result += 10;
	}

	// short <<= int: result type is promote_integer_type(short) = int
	// Then stored back as short. (short)1 << 2 = 4
	g_short <<= 2;
	if (g_short == 4) {
		result += 100;
	}

	// static int >>= int: 256 >> 3 = 32
	s_int >>= 3;
	if (s_int == 32) {
		result += 1000;
	}

	// Expected: 1111 (1 + 10 + 100 + 1000)
	return result - 1111;
}
