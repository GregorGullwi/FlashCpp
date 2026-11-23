// [dcl.fct]/5: a by-value parameter is initialized from the argument's value,
// so a top-level cv-qualifier on the argument's pointer object is dropped and
// the conversion stays an identity conversion. Pointee qualification still
// belongs to the compared type, so the pointees stay mutable here.
struct Cell {
	int value;
	int extra;
};

int read_row(int (*row)[3]) {
	return (*row)[1] + (*row)[2];
}

int bump_first(int (*row)[3]) {
	(*row)[0] += 1;
	return (*row)[0];
}

int read_cells(Cell (*rows)[2]) {
	return (*rows)[0].value + (*rows)[1].extra;
}

int read_row_const_param(int (* const row)[3]) {
	return (*row)[1];
}

int read_row_volatile_param(int (* volatile row)[3]) {
	return (*row)[2];
}

int main() {
	int values[2][3] = {{1, 2, 3}, {4, 5, 6}};
	int (*const const_rows)[3] = values;
	int (*volatile volatile_rows)[3] = &values[1];

	if (read_row(const_rows) != 5) {
		return 1;
	}
	if (read_row(volatile_rows) != 11) {
		return 2;
	}
	if (bump_first(const_rows) != 2) {
		return 3;
	}
	if (values[0][0] != 2) {
		return 4;
	}
	if (bump_first(volatile_rows) != 5) {
		return 5;
	}
	if (values[1][0] != 5) {
		return 6;
	}

	Cell cells[2][2] = {{{7, 8}, {9, 10}}, {{11, 12}, {13, 14}}};
	Cell (*const const_cells)[2] = cells;
	Cell (*volatile volatile_cells)[2] = &cells[1];
	if (read_cells(const_cells) != 17) {
		return 7;
	}
	if (read_cells(volatile_cells) != 25) {
		return 8;
	}
	// [dcl.fct]/5 deletes top-level cv from the parameter type as well, so a
	// plain pointer-to-array argument matches these declarations.
	int (*plain_rows)[3] = values;
	if (read_row_const_param(plain_rows) != 2) {
		return 9;
	}
	if (read_row_volatile_param(plain_rows) != 3) {
		return 10;
	}
	return 0;
}
