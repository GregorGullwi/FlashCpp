// Rejecting array-to-scalar and array-to-scalar-reference conversions must not
// disturb valid array-to-pointer decay, reference-to-array binding, or the
// array-to-bool conversion.
static int firstValue(int* values) {
	return values[0];
}

int main() {
	int values[3] = {1, 2, 3};
	int (&reference_to_array)[3] = values;
	if (firstValue(values) != 1) {
		return 1;
	}
	if (reference_to_array[1] != 2) {
		return 2;
	}
	if (!values) {
		return 3;
	}
	return 42;
}
