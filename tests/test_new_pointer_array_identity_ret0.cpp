// C++20 [expr.new]: new T has type T*. When the allocated object is an array,
// the result points at the initial element, so one outermost bound is removed.
// new Row for Row = int[4] is int*. new Grid for Grid = int[2][3] is int(*)[3].
// new Row[2] keeps Row and is int(*)[4]. Cv on the allocated object stays on
// that object; the result pointer itself is not const.
using Row = int[4];
using Grid = int[2][3];

struct Cell {
	int value;
	char tag;
};

template <class T>
struct Holder {
	T value;
};

int take_row(int*) { return 0; }
int take_row(int (*)[4]) { return 1; }

int take_grid(int*) { return 2; }
int take_grid(int (*)[3]) { return 0; }
int take_grid(int (*)[2][3]) { return 1; }

int take_const_scalar(int*) { return 1; }
int take_const_scalar(const int*) { return 0; }

int take_const_ptr(int**) { return 1; }
int take_const_ptr(int* const*) { return 0; }

int take_holder(Holder<long>*) { return 0; }
int take_holder(Holder<short>*) { return 1; }
int take_holder(void*) { return 2; }

int main() {
	int* scalar = new int(21);
	if (*scalar != 21) {
		return 1;
	}
	delete scalar;
	const int* const_scalar = new const int(21);
	if (*const_scalar != 21) {
		return 2;
	}
	int const_scalar_kind = take_const_scalar(new const int(21));
	if (const_scalar_kind != 0) {
		return 30 + const_scalar_kind;
	}
	delete const_scalar;

	int* values = new int[3];
	values[0] = 10;
	values[1] = 20;
	values[2] = 30;
	if (values[0] + values[1] + values[2] != 60 || take_row(values) != 0) {
		return 3;
	}
	delete[] values;

	int row_kind = take_row(new Row);
	if (row_kind != 0) {
		return 40 + row_kind;
	}
	int row_array_kind = take_row(new Row[2]);
	if (row_array_kind != 1) {
		return 50 + row_array_kind;
	}
	int (*rows)[4] = new Row[2];
	(*rows)[0] = 7;
	(*rows)[3] = 8;
	rows[1][0] = 9;
	if ((*rows)[3] != 8 || rows[1][0] != 9) {
		return 4;
	}
	delete[] rows;

	int grid_kind = take_grid(new Grid);
	if (grid_kind != 0) {
		return 60 + grid_kind;
	}
	int (*grid)[3] = new Grid;
	grid[0][0] = 1;
	grid[0][2] = 3;
	grid[1][0] = 4;
	grid[1][2] = 6;
	if (grid[0][2] != 3 || grid[1][0] != 4 || grid[1][2] != 6) {
		return 5;
	}
	delete[] grid;
	const int (*const_grid)[3] = new const Grid;
	delete[] const_grid;

	int const_ptr_kind = take_const_ptr(new int* const);
	if (const_ptr_kind != 0) {
		return 80 + const_ptr_kind;
	}
	int** pointer_array = new int*[2];
	int first = 11;
	int second = 12;
	pointer_array[0] = &first;
	pointer_array[1] = &second;
	if (*pointer_array[0] != 11 || *pointer_array[1] != 12) {
		return 6;
	}
	delete[] pointer_array;

	long* wide = new long(100L);
	if (*wide != 100L) {
		return 7;
	}
	delete wide;
	short* narrow = new short[2];
	narrow[0] = 7;
	narrow[1] = 8;
	if (narrow[0] != 7 || narrow[1] != 8) {
		return 8;
	}
	delete[] narrow;

	Cell* cells = new Cell[2];
	cells[0].value = 3;
	cells[0].tag = 'a';
	cells[1].value = 4;
	cells[1].tag = 'b';
	if (cells[0].tag != 'a' || cells[1].value != 4) {
		return 9;
	}
	delete[] cells;

	int holder_kind = take_holder(new Holder<long>{9L});
	if (holder_kind != 0) {
		return 90 + holder_kind;
	}
	Holder<long>* held = new Holder<long>{9L};
	if (held->value != 9L) {
		return 10;
	}
	delete held;
	Holder<short>* held_short = new Holder<short>[1];
	held_short[0].value = 4;
	if (held_short[0].value != 4 || take_holder(held_short) != 1) {
		return 11;
	}
	delete[] held_short;

	int storage = 0;
	int* placed = new (&storage) int(15);
	if (*placed != 15 || placed != &storage) {
		return 12;
	}
	return 0;
}
