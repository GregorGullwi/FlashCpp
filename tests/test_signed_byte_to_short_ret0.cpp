static short widen(signed char value) {
	return value;
}

int main() {
	signed char negative = -37;
	signed char positive = 23;
	short negative_short = negative;
	short positive_short = positive;
	if (negative_short != -37 || positive_short != 23) {
		return 1;
	}
	if (widen(negative) != -37 || widen(positive) != 23) {
		return 2;
	}
	return 0;
}
