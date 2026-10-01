// Temporary identity must remain distinct from local spellings and survive
// aggregate materialization and reference catches after the former 256 limit.

int keep(int value) {
	return value;
}

struct Mixed {
	char byte;
	short half;
	int word;
	long long wide;
	float single;
	double real;
};

consteval Mixed makeMixed() {
	return {7, 300, 40000, 9000000000LL, 1.5f, 2.5};
}

// Each step needs distinct call results that are simultaneously live, plus
// arithmetic results. The 160 steps put later operations well above 256.
#define PREFIX_1 accumulator = accumulator + (keep(1) + keep(2));
#define PREFIX_2 PREFIX_1 PREFIX_1
#define PREFIX_4 PREFIX_2 PREFIX_2
#define PREFIX_8 PREFIX_4 PREFIX_4
#define PREFIX_16 PREFIX_8 PREFIX_8
#define PREFIX_32 PREFIX_16 PREFIX_16
#define PREFIX_64 PREFIX_32 PREFIX_32
#define PREFIX_128 PREFIX_64 PREFIX_64

int main() {
	int accumulator = 0;
	PREFIX_128
	PREFIX_32
	if (accumulator != 480)
		return 1;

	Mixed result = makeMixed();
	if (result.byte != 7 || result.half != 300 || result.word != 40000 ||
		result.wide != 9000000000LL || result.single != 1.5f || result.real != 2.5)
		return 2;

	int first = 7;
	int second = 11;
	int third = 13;
	int& temp_0 = first;
	int& temp_255 = second;
	int& temp_INVALID = third;
	temp_0 = temp_255 + temp_INVALID;
	temp_255 = temp_0 - temp_INVALID;
	temp_INVALID = temp_255 + 2;
	if (first != 24 || second != 11 || third != 13)
		return 3;

	int caught_value = 0;
	try {
		throw 9;
	} catch (int& caught) {
		caught = caught + temp_0;
		caught_value = caught;
	}
	return caught_value == 33 ? 0 : 4;
}
