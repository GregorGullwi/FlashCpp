// A bool source must be converted to the target's full width before the value
// is read as an integer or floating-point scalar.
int main() {
	bool yes = true;
	bool no = false;

	int as_int = (int)yes;
	if (as_int != 1)
		return 1;

	unsigned int as_unsigned = static_cast<unsigned int>(yes);
	if (as_unsigned != 1u)
		return 2;

	long long as_long_long = (long long)yes;
	if (as_long_long != 1LL)
		return 3;

	double as_double = static_cast<double>(yes);
	if (as_double != 1.0)
		return 4;

	int false_as_int = static_cast<int>(no);
	if (false_as_int != 0)
		return 5;

	return 0;
}
