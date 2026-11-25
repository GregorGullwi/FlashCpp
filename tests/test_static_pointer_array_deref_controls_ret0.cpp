// Controls for the two-dimensional pointer-element lowering: a one-dimensional
// array of pointers, a pointer to a two-dimensional int array, and a row of a
// two-dimensional int array reached through a pointer. Their element shapes
// already index correctly and must keep doing so.
int first = 1;
int second = 2;
int third = 3;

int* row[3] = {&first, &second, &third};
int grid[2][3] = {{10, 20, 30}, {40, 50, 60}};

int main() {
	int* (*row_pointer)[3] = &row;
	if ((*row_pointer)[0] != &first || *(*row_pointer)[0] != 1) return 1;
	if ((*row_pointer)[2] != &third || *(*row_pointer)[2] != 3) return 2;

	int (*plane)[2][3] = &grid;
	if ((*plane)[0][0] != 10 || (*plane)[0][2] != 30) return 3;
	if ((*plane)[1][0] != 40 || (*plane)[1][2] != 60) return 4;

	int (*wide_row)[3] = &grid[1];
	if ((*wide_row)[0] != 40 || (*wide_row)[1] != 50 || (*wide_row)[2] != 60) return 5;

	int* row_start = &grid[1][0];
	if (row_start[0] != 40 || row_start[2] != 60) return 6;

	return 0;
}
