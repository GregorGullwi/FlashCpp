// C++20 [conv.array]/[expr.add]: `cells + 1` decays the array operand to
// `Cell*` before built-in pointer arithmetic. The element is a 16-byte record,
// so a stride taken from the element's builtin category (or from the pointer
// width) produces the wrong address and the checks below read the wrong cell.

struct Cell {
	long long a;
	long long b;
};

int main() {
	Cell cells[3];
	cells[0].a = 1;
	cells[1].a = 2;
	cells[2].a = 3;

	Cell* p = cells + 1;
	if (p != &cells[1]) {
		return 1;
	}
	if (p->a != 2) {
		return 2;
	}
	p->a = 20;
	if (cells[1].a != 20) {
		return 3;
	}

	Cell* commutative = 1 + cells;
	if (commutative != &cells[1] || commutative->a != 20) {
		return 4;
	}

	Cell* other = cells;
	long cells_to_other = cells - other;
	if (cells_to_other != 0) {
		return 5;
	}
	long other_to_cells = commutative - cells;
	if (other_to_cells != 1) {
		return 6;
	}
	long cells_to_commutative = cells - commutative;
	if (cells_to_commutative != -1) {
		return 7;
	}

	Cell* stepped = cells;
	stepped += 1;
	if (stepped != &cells[1]) {
		return 8;
	}
	stepped += 1;
	if (stepped != &cells[2] || stepped->a != 3) {
		return 9;
	}
	stepped[0].a = 30;
	if (cells[2].a != 30) {
		return 10;
	}

	// Controls: already-decayed pointers keep their own arithmetic.
	Cell* pointer = cells;
	Cell* next = pointer + 1;
	if (next != &cells[1] || next->a != 20) {
		return 11;
	}
	int values[2];
	values[0] = 7;
	values[1] = 9;
	int* value_pointer = values + 1;
	if (value_pointer != &values[1] || *value_pointer != 9) {
		return 12;
	}

	return 0;
}
