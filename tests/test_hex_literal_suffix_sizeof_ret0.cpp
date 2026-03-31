// Test: sizeof integer literals with suffixes per C++20 [lex.icon].

int main() {
 // U suffix -> unsigned int
	if (sizeof(0xFFU) != sizeof(unsigned int))
		return 1;
	if (sizeof(0xFFFFFFFFU) != sizeof(unsigned int))
		return 2;
	if (sizeof(0b1010U) != sizeof(unsigned int))
		return 3;
	if (sizeof(010U) != sizeof(unsigned int))
		return 4;

 // No suffix -> int
	if (sizeof(0xFF) != sizeof(int))
		return 5;

 // L suffix -> long
	if (sizeof(0xFFL) != sizeof(long))
		return 6;

 // LL suffix -> long long
	if (sizeof(0xFFLL) != sizeof(long long))
		return 7;

 // UL suffix -> unsigned long
	if (sizeof(0xFFUL) != sizeof(unsigned long))
		return 8;

	return 0;
}
