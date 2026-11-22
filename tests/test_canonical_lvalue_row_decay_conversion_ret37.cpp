// Reduced regression for canonical planning of lvalue-marked multidimensional
// array decay: an ordinary array argument records its value category with a
// reference marker, and a sized array with more than one extent must convert
// to its row pointer through the canonical projectable plan instead of the
// syntax compatibility tail. The returned sum depends on the row stride and
// on the innermost extents remaining part of the pointee type.

static int acceptIntRows(int (*rows)[3]) {
	return rows[0][0] + rows[1][2];
}

static int acceptLongLongRows(long long (*rows)[3]) {
	return static_cast<int>(rows[0][1] + rows[1][0]);
}

static int acceptIntCube(int (*rows)[3][4]) {
	return rows[1][2][3];
}

int main() {
	int rows[2][3] = {{1, 2, 3}, {4, 5, 6}};
	long long long_rows[2][3] = {{1, 2, 3}, {4, 5, 6}};
	int cube[2][3][4] = {
		{{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}},
		{{13, 14, 15, 16}, {17, 18, 19, 20}, {21, 22, 23, 24}}};
	return acceptIntRows(rows) + acceptLongLongRows(long_rows) + acceptIntCube(cube);
}
