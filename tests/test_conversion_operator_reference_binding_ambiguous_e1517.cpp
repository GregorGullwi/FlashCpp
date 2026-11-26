// Direct reference binding through a conversion function is ambiguous when two
// different conversion functions both bind the same reference type. Reference
// compatibility makes both second sequences identity conversions, so the
// cv-qualification of the referenced type does not order them (CWG 233).
struct ConversionPair {
	operator int&();

	operator const int&();
};

int main() {
	ConversionPair value;
	const int& alias = value;
	return alias;
}
