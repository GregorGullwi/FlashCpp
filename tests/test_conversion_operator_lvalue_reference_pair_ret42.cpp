// For an int& target only operator int&() can bind the reference directly;
// operator const int&() would drop const, so the two conversion functions do
// not tie and the initialization is unambiguous.
struct ConversionPair {
	int storage;

	operator int&() { return storage; }

	operator const int&() { return storage; }
};

int main() {
	ConversionPair holder{21};
	int& alias = holder;
	alias += 21;
	return alias;
}
