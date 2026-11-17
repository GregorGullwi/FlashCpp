// An array parameter whose elements are pointers to arrays must decay only its
// outer array layer, preserving the pointer-to-array element shape.

int sum_row(int (*cells[2])[4]) {
	return cells[0][0][0] + cells[1][0][3];
}

int first_cell(int (*cells[2])[4]) {
	return cells[0][0][0];
}

int main() {
	static int first[4] = {1, 2, 3, 4};
	static int second[4] = {5, 6, 7, 8};
	int (*cells[2])[4] = {&first, &second};
	if (sum_row(cells) != 9) {
		return 1;
	}
	if (first_cell(cells) != 1) {
		return 2;
	}
	return 0;
}
