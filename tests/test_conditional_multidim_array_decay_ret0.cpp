// C++20 [expr.cond] applies [conv.array] before the common type is chosen.
// int[2][3] decays to int(*)[3], and int[][2] decays to int(*)[2]. Rewriting the
// flat dimension list instead produced an array of pointers (or a pointer to
// the scalar element). The classifiers return that wrong rank; the fixed
// decay selects the pointer-to-array overload and the row stride below.
struct Cell {
	int value;
	char tag;
};

int classify_grid(int**) { return 1; }
int classify_grid(int (*)[3]) { return 3; }

int classify_rows(int*) { return 1; }
int classify_rows(int (*)[2]) { return 2; }

int classify_const(const int**) { return 1; }
int classify_const(const int (*)[2]) { return 3; }

int classify_wide(long**) { return 1; }
int classify_wide(long (*)[2]) { return 2; }

int classify_cells(Cell**) { return 1; }
int classify_cells(Cell (*)[2]) { return 2; }

template <class T>
struct PairGrid {
	T cells[2][2];
};

int classify_pair(int**) { return 1; }
int classify_pair(int (*)[2]) { return 2; }

int main() {
	int grid[2][3] = {{1, 2, 3}, {4, 5, 6}};
	int grid_code = classify_grid(true ? grid : grid);
	if (grid_code != 3) {
		return grid_code;
	}
	int rows[][2] = {{7, 8}, {9, 10}, {11, 12}};
	int rows_code = classify_rows(true ? rows : rows);
	if (rows_code != 2) {
		return 10 + rows_code;
	}
	const int const_rows[2][2] = {{11, 12}, {13, 14}};
	int const_code = classify_const(true ? const_rows : const_rows);
	if (const_code != 3) {
		return 20 + const_code;
	}
	long wide[2][2] = {{100L, 200L}, {300L, 400L}};
	int wide_code = classify_wide(true ? wide : wide);
	if (wide_code != 2) {
		return 30 + wide_code;
	}
	Cell cells[2][2] = {{{1, 'a'}, {2, 'b'}}, {{3, 'c'}, {4, 'd'}}};
	int cell_code = classify_cells(true ? cells : cells);
	if (cell_code != 2) {
		return 40 + cell_code;
	}
	short matrix[2][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}};

	int (*row)[3] = true ? grid : grid;
	if (row[1][0] != 4 || (*row)[2] != 3) {
		return 4;
	}
	int (*unsized_row)[2] = true ? rows : rows;
	if (unsized_row[2][0] != 11 || unsized_row[1][1] != 10) {
		return 5;
	}
	const int (*const_row)[2] = true ? const_rows : const_rows;
	if (const_row[1][1] != 14) {
		return 6;
	}
	long (*wide_row)[2] = true ? wide : wide;
	if (wide_row[1][0] != 300L) {
		return 7;
	}
	Cell (*cell_row)[2] = true ? cells : cells;
	if (cell_row[1][0].value != 3 || cell_row[0][1].tag != 'b') {
		return 8;
	}
	short (*matrix_row)[4] = true ? matrix : matrix;
	if (matrix_row[1][3] != 8) {
		return 9;
	}
	PairGrid<int> pairs{{{8, 9}, {10, 11}}};
	int pair_code = classify_pair(true ? pairs.cells : pairs.cells);
	if (pair_code != 2) {
		return 60 + pair_code;
	}
	int (*pair_row)[2] = true ? pairs.cells : pairs.cells;
	if (pair_row[1][0] != 10) {
		return 12;
	}
	return 0;
}
