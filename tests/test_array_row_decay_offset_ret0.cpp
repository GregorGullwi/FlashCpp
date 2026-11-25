// C++20 [conv.array]/[expr.sub]: `grid[1]` selects the second row, and
// decaying that row must point at grid[1][0]. A stride taken from the converted
// pointer result instead of the row's canonical element size lands on
// grid[0][2] and every check below fails or crashes.

struct Cell {
	long long a;
	long long b;
};

int main() {
	int grid[2][3];
	grid[0][0] = 1;
	grid[0][2] = 3;
	grid[1][0] = 4;
	grid[1][1] = 5;
	grid[1][2] = 6;

	int* row = grid[1];
	if (row != &grid[1][0]) {
		return 1;
	}
	if (row[0] != 4 || row[2] != 6) {
		return 2;
	}
	row[0] = 40;
	row[2] = 60;
	if (grid[1][0] != 40 || grid[1][2] != 60) {
		return 3;
	}
	// Control: the explicit element address and pointer-to-row forms.
	if (grid[0][2] != 3) {
		return 4;
	}
	int* explicit_element = &grid[1][0];
	if (explicit_element != row || *explicit_element != 40) {
		return 5;
	}
	int (*row_array)[3] = &grid[1];
	if ((*row_array)[0] != 40 || (*row_array)[2] != 60) {
		return 6;
	}

	int cube[2][2][3];
	cube[1][0][0] = 100;
	cube[1][0][2] = 102;
	cube[1][1][0] = 110;
	cube[1][1][2] = 112;

	// cube[1][0] is an int[3] row; decaying it selects cube[1][0][0].
	int* cube_row = cube[1][0];
	if (cube_row != &cube[1][0][0] || cube_row[2] != 102) {
		return 7;
	}
	// cube[1] is an int[2][3] row; decaying it yields a pointer to its first
	// int[3] row.
	int (*cube_plane)[3] = cube[1];
	if (cube_plane[0][0] != 100 || cube_plane[0][2] != 102) {
		return 8;
	}
	if (cube_plane[1][0] != 110 || cube_plane[1][2] != 112) {
		return 15;
	}
	cube_row[0] = 101;
	if (cube[1][0][0] != 101) {
		return 9;
	}

	Cell cells[2][3];
	cells[0][0].a = 2;
	cells[0][2].a = 6;
	cells[0][2].b = 7;
	cells[1][0].a = 20;
	cells[1][2].a = 60;

	Cell* record_row = cells[1];
	if (record_row != &cells[1][0]) {
		return 10;
	}
	if (record_row[0].a != 20 || record_row[2].a != 60) {
		return 11;
	}
	record_row[0].b = 21;
	record_row[2].b = 61;
	if (cells[1][0].b != 21 || cells[1][2].b != 61) {
		return 12;
	}
	if (cells[0][2].b != 7) {
		return 13;
	}
	Cell (*record_row_array)[3] = &cells[1];
	if ((*record_row_array)[0].b != 21 || (*record_row_array)[2].b != 61) {
		return 14;
	}

	return 0;
}
