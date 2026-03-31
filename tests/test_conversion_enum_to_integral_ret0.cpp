// Test: enum-to-integral conversions and ranking.
// Confirms enum->int promotion still beats wider integral conversions.

enum SmallEnum { Zero = 0,
				 Seven = 7 };

int pickEnum(int value) {
	return value;
}

int pickEnum(long long value) {
	return (int)(value + 100);
}

double widenEnum(SmallEnum value) {
	return value;
}

int main() {
	SmallEnum value = Seven;

	if (pickEnum(value) != 7)
		return 1;

	long long widened = value;
	if (widened != 7)
		return 2;

	double as_double = widenEnum(value);
	if ((int)as_double != 7)
		return 3;

	bool as_bool = value;
	if (!as_bool)
		return 4;

	return 0;
}
