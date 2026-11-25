// C++20 [expr.eq]/[expr.rel]: a pointer-to-array is one pointer, so comparing
// two `int (*)[3]` values reads the whole pointer. The declaration's flat
// element width is the row's `int`, and it must not narrow the load of a
// static local or shorten the namespace-scope initializer stride.
//
// A namespace-scope initializer such as `&grid[1]` records a relocation with
// the row's object size (12 bytes), and a static local load reads 64 bits. A
// flat element-size stride lands on `grid[0][1]`, so the equality and the row
// contents below fail.

struct Row {
	long long first;
	long long second;
};

static int global_grid[2][3] = {{1, 2, 3}, {4, 5, 6}};
static int first_large[512][3] = {};
static int second_large[512][3] = {};

static int (*global_row)[3] = &global_grid[1];
static int (*global_first)[3] = &global_grid[0];

static const int const_grid[2][3] = {{7, 8, 9}, {10, 11, 12}};
static const int (*global_const_row)[3] = &const_grid[1];

static Row record_grid[2][3];

static int (*large_row)[3] = &first_large[400];

int main() {
	record_grid[1][0].first = 41;
	record_grid[1][2].second = 43;

	// Namespace-scope pointer-to-array initializers step by the complete row.
	if (global_row != &global_grid[1]) {
		return 1;
	}
	if (global_first != &global_grid[0]) {
		return 2;
	}
	if (global_row == &global_grid[0]) {
		return 3;
	}
	if ((*global_row)[0] != 4 || (*global_row)[2] != 6) {
		return 4;
	}
	if (global_first == global_row || global_first > global_row) {
		return 5;
	}
	if (!(global_row > global_first) || !(global_first < global_row)) {
		return 6;
	}

	// const variants keep the same pointer value and row access.
	if (global_const_row != &const_grid[1]) {
		return 7;
	}
	if ((*global_const_row)[1] != 11) {
		return 8;
	}

	// A record row pointer compares the row address and then reads the row.
	Row (*record_row)[3] = &record_grid[1];
	if (record_row != &record_grid[1]) {
		return 9;
	}
	if (record_row == &record_grid[0]) {
		return 10;
	}
	if ((*record_row)[0].first != 41 || (*record_row)[2].second != 43) {
		return 24;
	}

	// A static local pointer-to-array is still one 64-bit pointer value.
	static int (*static_row)[3] = &global_grid[1];
	if (static_row != &global_grid[1]) {
		return 11;
	}
	if (static_row != global_row) {
		return 12;
	}
	if (!(static_row >= global_row) || static_row < global_row) {
		return 13;
	}
	if ((*static_row)[1] != 5) {
		return 14;
	}

	// Rows inside one large static object keep their order across a wide span.
	if (large_row != &first_large[400]) {
		return 15;
	}
	if (!(large_row + 1 > large_row)) {
		return 16;
	}
	if (large_row + 1 != &first_large[401]) {
		return 17;
	}
	if (&second_large[0] == &first_large[0]) {
		return 18;
	}

	// Local rows and one-past arithmetic are controls for the same shape.
	int local_grid[2][3] = {{1, 2, 3}, {4, 5, 6}};
	int (*local_row)[3] = &local_grid[1];
	if (local_row != &local_grid[1]) {
		return 19;
	}
	if (local_row == &local_grid[0]) {
		return 20;
	}
	if (!(local_row > &local_grid[0])) {
		return 21;
	}
	if (local_row + 1 != local_grid + 2) {
		return 22;
	}
	if (local_row - 1 != local_grid) {
		return 23;
	}

	return 0;
}
