// C++20 [expr.add]/1: pointer plus integer and integer plus pointer remain
// built-in candidates after two pointer operands are rejected. The left-to-right
// form, the commutative form, subtraction, and the pointer-to-array shapes all
// scale by the canonical pointee object.

struct Cell {
	long long value;
};

int main() {
	int values[4] = {1, 2, 3, 4};
	int* first = values + 1;
	int* commutated = 2 + values;
	if (first != &values[1] || commutated != &values[2]) {
		return 1;
	}
	if (first + 1 != &values[2] || 1 + first != &values[2]) {
		return 2;
	}
	if (first - 1 != values || first - 2 != values - 1) {
		return 3;
	}
	if ((commutated + 1) - first != 2) {
		return 4;
	}

	Cell cells[3];
	cells[1].value = 11;
	Cell* cell = cells + 1;
	Cell* cell_commutated = 1 + cells;
	if (cell != &cells[1] || cell_commutated != &cells[1]) {
		return 5;
	}
	if (cell->value != 11) {
		return 6;
	}

	int grid[2][3] = {{1, 2, 3}, {4, 5, 6}};
	int (*row)[3] = grid + 1;
	int (*row_commutated)[3] = 1 + grid;
	if (row != &grid[1] || row_commutated != &grid[1]) {
		return 7;
	}
	if ((*row)[0] != 4 || (*row_commutated)[2] != 6) {
		return 8;
	}
	if (row + 1 != grid + 2 || row - 1 != grid) {
		return 9;
	}
	if (row - grid != 1) {
		return 10;
	}

	return 0;
}
