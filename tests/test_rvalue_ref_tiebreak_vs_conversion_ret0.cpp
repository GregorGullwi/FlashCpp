// Regression test for the C++20 [over.ics.rank]/3.2.3 tie-breaker:
// when an rvalue can bind to either T&& or const T&, the T&& overload wins,
// but const T& must still remain an ExactMatch-category binding relative to
// unrelated non-reference candidates like `long`.

int chooseRefKind(int&&) {
	return 1;
}

int chooseRefKind(const int&) {
	return 2;
}

int chooseConstRefOrLong(const int&) {
	return 4;
}

int chooseConstRefOrLong(long) {
	return 5;
}

int main() {
	int value = 7;

	if (chooseRefKind((int&&)value) != 1)
		return 1;

	if (chooseRefKind(value) != 2)
		return 2;

	// The reference-binding tie-breaker must not demote const T& so far that it
	// ties with an unrelated standard conversion candidate.
	if (chooseConstRefOrLong((int&&)value) != 4)
		return 3;

	if (chooseConstRefOrLong(value) != 4)
		return 4;

	return 0;
}
