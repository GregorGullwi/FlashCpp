// Reduced regression for canonical planning of lvalue-marked builtin array
// decay: an ordinary array argument records its value category with a
// reference marker, and every supported by-value array decay shape now
// converts through the canonical projectable plan instead of the syntax
// compatibility tail: element pointers, pointer-element arrays decaying to
// pointer-to-pointer, pointer-to-row targets, cv-void targets, and arrays of
// bool. This file must keep the compatibility fallback counter at zero.

static int acceptIntPointer(int* values) {
	return values[0] + values[1];
}

static int acceptConstIntPointer(const int* values) {
	return values[0] + values[1];
}

static int acceptConstBoolPointer(const bool* values) {
	return values[0] ? 16 : 0;
}

static int acceptVoidPointer(void* values) {
	return values == nullptr ? 0 : 8;
}

static int acceptPointerPointer(int** values) {
	return *values[0];
}

static int acceptRowPointer(int (*rows)[3]) {
	return rows[1][0];
}

int main() {
	int values[2] = {3, 4};
	const int const_values[2] = {5, 6};
	bool flags[2] = {true, false};
	char letters[2] = {'A', 'B'};
	int* pointer_values[2] = {&values[0], &values[1]};
	int matrix[2][3] = {{1, 2, 3}, {11, 12, 13}};
	return acceptIntPointer(values) + acceptConstIntPointer(const_values) +
		acceptConstBoolPointer(flags) + acceptVoidPointer(letters) +
		acceptPointerPointer(pointer_values) + acceptRowPointer(matrix);
}
