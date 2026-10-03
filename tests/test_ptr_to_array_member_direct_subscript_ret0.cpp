// Direct subscripting of a pointer-to-array member preserves the row bound.
int rows[2][3] = {{1, 2, 3}, {4, 5, 6}};

struct Grid {
	int padding[2];
	int (*cells)[3];
};

int main() {
	Grid grid;
	grid.cells = rows;
	if (grid.cells[0][2] != 3) return 1;
	if (grid.cells[1][1] != 5) return 2;
	grid.cells[1][2] = 9;
	if (rows[1][2] != 9) return 3;

	Grid& alias = grid;
	if (alias.cells[0][1] != 2) return 4;
	alias.cells[0][0] = 10;
	if (rows[0][0] != 10) return 5;
	return 0;
}
