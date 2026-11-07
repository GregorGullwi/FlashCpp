struct Rows {
	short (*pointer)[3];
};

int main() {
	short grid[2][3] = {{1, 2, 3}, {4, 5, 6}};
	Rows rows{grid};
	if (sizeof(*rows.pointer) != 3 * sizeof(short)) {
		return 1;
	}
	if ((*rows.pointer)[2] != 3) {
		return 2;
	}
	short (*selected)[3] = true ? rows.pointer : &grid[1];
	return (*(true ? rows.pointer : selected))[1] == 2 ? 0 : 3;
}
