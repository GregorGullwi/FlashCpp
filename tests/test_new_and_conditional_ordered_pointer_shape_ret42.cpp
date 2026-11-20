// [expr.new]: an allocated array result points at its initial element, so the
// outermost bound becomes the pointer and later bounds stay on that element.
// The shape is expressed on the ordered declarator spine instead of rebuilding a
// flat extent list. [expr.cond] compares pointer shapes, and integral promotion
// only applies when the runtime object is not a pointer.
using Row = int[4];
using Grid = int[2][3];
using PtrRow = int*[2];

int take_row(int*) { return 0; }
int take_row(int (*)[4]) { return 1; }
int take_grid(int*) { return 2; }
int take_grid(int (*)[3]) { return 0; }
int take_ptr_row(int**) { return 2; }
int take_ptr_row(int* (*)[2]) { return 0; }

int main() {
	if (take_row(new Row) != 0) return 1;
	if (take_row(new Row[2]) != 1) return 2;
	if (take_grid(new Grid) != 0) return 3;
	if (take_ptr_row(new PtrRow[3]) != 0) return 4;
	int storage[2][3] = {{1, 2, 3}, {4, 5, 6}};
	int* rows[2];
	rows[0] = storage[0];
	rows[1] = storage[1];
	int** plain = rows;
	auto common = true ? rows : plain;
	if (sizeof(common) != sizeof(void*)) return 5;
	auto address = &rows;
	if (sizeof(address) != sizeof(void*)) return 6;
	auto promoted = +7;
	if (sizeof(promoted) != sizeof(int)) return 7;
	return 42;
}
