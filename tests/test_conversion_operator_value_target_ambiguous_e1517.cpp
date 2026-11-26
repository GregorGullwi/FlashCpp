// The same [over.ics.rank]/3.3 rule applies to a by-value target: the two
// conversion functions select different user-defined conversions, so their
// identical second standard conversion sequences do not order them.
struct ConversionPair {
	operator int&();

	operator const int&();
};

int main() {
	ConversionPair value;
	int target = value;
	return target;
}
