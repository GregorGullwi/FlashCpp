// [over.match.sub] and [expr.sub]: a class conversion to a pointer-to-array
// makes E1[E2] an lvalue of the array type. The adopted pointee keeps every
// bound, so sizeof observes the whole row and a second subscript selects the
// correct element of each row instead of stepping by one scalar.
typedef int Row[3];

struct RowPtr {
	int data[2][3];

	operator Row*() {
		return &data[0];
	}
};

int main() {
	RowPtr rows;
	rows.data[0][0] = 10;
	rows.data[0][1] = 0;
	rows.data[0][2] = 0;
	rows.data[1][0] = 0;
	rows.data[1][1] = 32;
	rows.data[1][2] = 0;

	if (sizeof(rows[0]) != sizeof(Row)) {
		return 1;
	}
	return rows[0][0] + rows[1][1];
}
