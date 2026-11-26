// [over.ics.rank]/3.3: the two user-defined conversion sequences select
// different conversion functions, so their identity second sequences are not
// ordered against each other and the call is ambiguous.
struct ConversionPair {
	operator int&();

	operator const int&();
};

void choose(int&);

void choose(const int&);

int main() {
	ConversionPair value;
	choose(value);
}
