// C++20 [over.ics.rank]/3.3: two user-defined conversion sequences are ordered by
// a standard conversion sequence only when they contain the same user-defined
// conversion function or constructor. Two different converting constructors give
// indistinguishable implicit conversion sequences, so this call must stay
// ambiguous even though one parameter is reached by an exact match and the other
// by an integral conversion.
//
// Do not "fix" this by ranking the leading standard conversion of a
// converting-constructor conversion: MSVC (C2668), GCC, and Clang all reject it.
struct ScalarExact {
	ScalarExact(short value) : stored(value) {}
	int stored;
};

struct ScalarConverted {
	ScalarConverted(long value) : stored(static_cast<int>(value)) {}
	int stored;
};

int select_conversion(ScalarExact target) {
	return target.stored;
}

int select_conversion(ScalarConverted target) {
	return target.stored + 100;
}

int main() {
	short argument = 40;
	return select_conversion(argument);
}
