// Reduced regression for canonical planning of an lvalue-marked builtin source:
// the value-category marker on `narrow` and `wide` must not send these argument
// conversions to the syntax compatibility tail, and overload ranking must keep
// the integral promotion ahead of the integral conversion and the exact match
// ahead of both.

static int select(int) {
	return 1;
}

static int select(long) {
	return 2;
}

int main() {
	char narrow = 0;
	long wide = 0;
	return select(narrow) + select(wide) * 10;
}
