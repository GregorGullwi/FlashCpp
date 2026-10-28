int side_effects = 0;

long long next_value(long long value) {
	++side_effects;
	return value;
}

short next_short(short value) {
	++side_effects;
	return value;
}

struct ScalarBase { long long value; };
struct ScalarKey : ScalarBase { short tag; };

struct WideBase {
	long long signed_value;
	unsigned long long unsigned_value;
	short mixed_short;
};
struct WideKey : WideBase { unsigned char tag; };

struct Padding { long long guard; };
struct First { short first; };
struct Second { long long second; };
struct MultiBase : Padding, First, Second { char tail; };

struct EmptyBase {};
struct BracedKey : EmptyBase { int value; };

int main() {
	ScalarKey scalar{7, {1}};
	if (scalar.value != 7 || scalar.tag != 1) return 1;

	WideKey wide{-5, 4000000000ULL, 9, {3}};
	if (wide.signed_value != -5 || wide.unsigned_value != 4000000000ULL || wide.mixed_short != 9 || wide.tag != 3) return 2;

	MultiBase multiple{11, 22, 33, 44};
	if (multiple.guard != 11 || multiple.first != 22 || multiple.second != 33 || multiple.tail != 44) return 3;

	ScalarKey controlled{{next_value(5)}, {next_short(6)}};
	if (controlled.value != 5 || controlled.tag != 6 || side_effects != 2) return 4;

	ScalarKey elapsed{next_value(7), {next_short(8)}};
	if (elapsed.value != 7 || elapsed.tag != 8 || side_effects != 4) return 5;

	BracedKey braced{{}, 9};
	if (braced.value != 9) return 6;

	return 0;
}
