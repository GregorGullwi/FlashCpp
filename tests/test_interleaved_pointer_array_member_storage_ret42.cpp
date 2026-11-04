// C++20 [dcl.array]/[dcl.ptr]: in `int (*cells[3])[4]` the inline object is an
// array of three pointers; each element points to `int[4]`. Member layout must
// use that pointer element size and alignment, not the flat base `int`.
struct IntCells {
	int (*cells[3])[4];
	char tail;
};

struct DoubleRows {
	double (*rows[2])[3];
	int after;
};

int storage[4] = {7, 8, 9, 10};
double doubles[3] = {1.5, 2.5, 3.5};

int main() {
	IntCells cells;
	if (sizeof(cells.cells) != 3 * sizeof(void*)) return 1;
	if (sizeof(IntCells) != 32) return 2;
	cells.cells[0] = &storage;
	cells.cells[1] = &storage;
	cells.cells[2] = &storage;
	cells.tail = 'x';
	if (cells.cells[0][0][0] != 7) return 3;
	if (cells.cells[2][0][3] != 10) return 4;

	DoubleRows rows;
	if (sizeof(rows.rows) != 2 * sizeof(void*)) return 5;
	if (sizeof(DoubleRows) != 24) return 6;
	rows.rows[0] = &doubles;
	rows.rows[1] = &doubles;
	if (rows.rows[0][0][0] != 1.5) return 7;
	if (rows.rows[1][0][2] != 3.5) return 8;
	return 42;
}
