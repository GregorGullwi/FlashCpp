// The valid neighbours of the canonical array-decay family: an array decays to
// a pointer to its first element, multidimensional arrays decay to a
// pointer-to-row, pointer-element arrays decay to pointer-to-pointer, and the
// row pointer still participates in ordinary pointer qualification.

static int values[3] = {1, 2, 3};
static long long wide_values[2] = {4, 5};
static int rows[2][3] = {{6, 7, 8}, {9, 10, 11}};
static double doubles[2][2] = {{12.0, 13.0}, {14.0, 15.0}};
static int a = 16, b = 17;
static int* pointers[2] = {&a, &b};
static const int* const_pointers[2] = {&a, &b};

static int readFirst(int* pointer) {
	return *pointer;
}

static long long readWideFirst(long long* pointer) {
	return *pointer;
}

static int readLastRow(int (*row)[3]) {
	return row[1][2];
}

static double readDoubleRow(double (*row)[2]) {
	return row[1][1];
}

static int readThroughPointers(int** pointer) {
	return **pointer;
}

static int readConstThroughPointers(const int** pointer) {
	return **pointer;
}

static void* eraseType(int* pointer) {
	return pointer;
}

int main() {
	if (readFirst(values) != 1) {
		return 1;
	}
	if (readWideFirst(wide_values) != 4) {
		return 2;
	}
	if (readLastRow(rows) != 11) {
		return 3;
	}
	if (readDoubleRow(doubles) != 15.0) {
		return 4;
	}
	if (readThroughPointers(pointers) != 16) {
		return 5;
	}
	if (readConstThroughPointers(const_pointers) != 16) {
		return 6;
	}
	int (*qualified_row)[3] = rows;
	if (readLastRow(qualified_row) != 11) {
		return 7;
	}
	const int (*const_row)[3] = rows;
	const int* const* qualified_pointers = const_pointers;
	if (qualified_pointers[0] != &a) {
		return 8;
	}
	if (eraseType(values) == nullptr) {
		return 9;
	}
	return 0;
}
