template<class T>
struct Holder {
	T value;
};

template<class Source>
Holder<long long> widenSigned(Source value) {
	return Holder<long long>{value};
}

template<class Source>
Holder<unsigned long long> widenUnsigned(Source value) {
	return Holder<unsigned long long>{value};
}

struct MixedWidths {
	long long signed_char;
	unsigned long long unsigned_short;
	long long signed_int;
	unsigned long long unsigned_int;
};

int negativeSeed() {
	return -23;
}

struct WidenPaths {
	long long scalar;
	long long values[2];
	double floating;
	long long defaulted = negativeSeed();
};

MixedWidths makeMixed(signed char c, unsigned short s, int i, unsigned int u) {
	return MixedWidths{c, s, i, u};
}

WidenPaths makeWidenPaths(int value, short small, char tiny, float floating) {
	return WidenPaths{{value}, {small, tiny}, floating};
}

int main() {
	Holder<long long> signed_from_int = widenSigned(-42);
	Holder<long long> signed_from_positive_int = widenSigned(42);
	Holder<long long> signed_from_char = widenSigned(static_cast<signed char>(-7));
	Holder<long long> signed_from_positive_char = widenSigned(static_cast<signed char>(7));
	Holder<unsigned long long> unsigned_from_int = widenUnsigned(0xF1234567u);
	Holder<unsigned long long> unsigned_from_char = widenUnsigned(static_cast<unsigned char>(231));
	MixedWidths mixed = makeMixed(static_cast<signed char>(-9), 65000, -1234567, 0xE1234567u);
	WidenPaths paths = makeWidenPaths(-31, -1234, 67, 1.25f);
	if (signed_from_int.value != -42 || signed_from_positive_int.value != 42 || signed_from_char.value != -7 || signed_from_positive_char.value != 7) {
		return 1;
	}
	if (unsigned_from_int.value != 0xF1234567ULL || unsigned_from_char.value != 231) {
		return 2;
	}
	if (mixed.signed_char != -9 || mixed.unsigned_short != 65000 || mixed.signed_int != -1234567 || mixed.unsigned_int != 0xE1234567ULL) { return 3; }
	return paths.scalar == -31 && paths.values[0] == -1234 && paths.values[1] == 67 && paths.floating == 1.25 && paths.defaulted == -23 ? 0 : 4;
}
