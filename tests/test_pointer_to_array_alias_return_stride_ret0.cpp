// A pointer-to-array alias return must preserve the pointee row extent. The
// call result may still use the flat projectable shape, but it must not become
// an array of pointers.

static int rows[2][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}};
using RowPointer = int (*)[4];

RowPointer get_rows() {
	return rows;
}

int (*get_rows_direct())[4] {
	return rows;
}

int main() {
	if (get_rows()[1][0] != 5) {
		return 1;
	}
	if (get_rows()[1][3] != 8) {
		return 2;
	}
	if (get_rows_direct()[1][0] != 5) {
		return 3;
	}
	return 0;
}
