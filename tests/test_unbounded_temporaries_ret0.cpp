// Each expansion creates fresh IR temporaries in the same function.
// Keep this source compact without relying on template recursion or library names.
#define REPEAT_2(X) X X
#define REPEAT_4(X) REPEAT_2(REPEAT_2(X))
#define REPEAT_16(X) REPEAT_4(REPEAT_4(X))
#define REPEAT_256(X) REPEAT_16(REPEAT_16(X))

struct Payload {
	long long wide;
	double fraction;
	int small;
};

template<class T> T identity(T value) {
	return value;
}

int exercise(int input) {
	int total = 0;
	int temp_0 = 19;
	int temp_255 = 23;
	int temp_INVALID = 29;
	REPEAT_256(total = total + (input * 3 + 1);)
	REPEAT_256(total = total + (input * 5 - 2);)
	if (total != 512 * input * 4 - 256) return 1;
	if (temp_0 != 19 || temp_255 != 23 || temp_INVALID != 29) return 2;
	Payload result{0x123456789LL + input, 2.5, input};
	long long wide = identity(result.wide) + 7;
	double fraction = identity(result.fraction) + 0.25;
	short small = identity((short)input);
	int* pointer = &total;
	int& reference = total;
	if (wide != 0x123456789LL + input + 7) return 3;
	if (fraction != 2.75 || small != input || result.small != input) return 4;
	reference = reference + input;
	if (*pointer != 512 * input * 4 - 256 + input) return 5;
	return 0;
}

int main() {
	int first = exercise(2);
	if (first != 0) return first;
	int second = exercise(7);
	if (second != 0) return second + 10;
	return 0;
}
