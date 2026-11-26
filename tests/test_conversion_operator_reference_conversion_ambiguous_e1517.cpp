// A single function parameter of type const int& still needs a conversion
// function selected for its argument. Both conversion functions bind the
// reference directly, so the argument is an ambiguous conversion sequence and
// the call is ill-formed even though only one function is viable.
struct ConversionPair {
	operator int&();

	operator const int&();
};

int observe(const int&);

int main() {
	ConversionPair value;
	return observe(value);
}
