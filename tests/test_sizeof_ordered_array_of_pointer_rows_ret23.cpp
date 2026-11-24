// sizeof of an interleaved ordered declarator sizes the outer array from the
// canonical array shape. `cells` is an array of three pointers, not three rows:
// a walk that multiplies the row extent before the pointer would report 48.
int main() {
	int (*cells[3])[4];
	if (sizeof(cells) != 24) {
		return 1;
	}
	if (sizeof(cells) / sizeof(cells[0]) != 3) {
		return 2;
	}
	if (sizeof(cells[0]) != 8) {
		return 3;
	}
	if (sizeof(*cells) != 8) {
		return 4;
	}
	if (sizeof(**cells) != 16) {
		return 5;
	}

	int grid[3][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}};
	cells[0] = grid;
	cells[1] = grid + 1;
	cells[2] = grid + 2;
	if (cells[2][0][3] != 12) {
		return 6;
	}

	return 23;
}
