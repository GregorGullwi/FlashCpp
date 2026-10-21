// C++20 [expr.unary.op] and [conv.array]. Unary * on an array removes one
// outermost bound and yields that element. Unary & does not decay: an array
// operand yields a pointer to the array. Cv on a pointer object does not
// qualify the result, and cv on an array transfers to the element. The old
// flat rewrite left * of an array loading the array bytes as a pointer.
struct Cell {
	int value;
	char tag;
};

struct Overloaded {
	int value;
	int operator*() const { return value + 1; }
	int* operator&() { return &value; }
};

template <class T>
struct PairGrid {
	T cells[2][2];
};

int id(int value) { return value; }

int classify_addr_grid(int (*)[3]) { return 1; }
int classify_addr_grid(int (*whole)[2][3]) { return (*whole)[1][0] == 4 ? 0 : 6; }

int classify_const_pointee(int*) { return 1; }
int classify_const_pointee(const int* pointer) { return *pointer == 21 ? 0 : 8; }

int main() {
	int grid[2][3] = {{1, 2, 3}, {4, 5, 6}};
	if ((*grid)[2] != 3) {
		return 1;
	}
	int (*grid_row)[3] = &(*grid);
	if (grid_row[1][0] != 4 || (*grid_row)[0] != 1) {
		return 2;
	}
	int rows[][2] = {{7, 8}, {9, 10}, {11, 12}};
	if ((*rows)[1] != 8) {
		return 3;
	}
	int (*unsized_row)[2] = &(*rows);
	if (unsized_row[1][0] != 9 || unsized_row[2][1] != 12) {
		return 4;
	}
	int values[3] = {10, 20, 30};
	if (*values != 10 || *(values + 1) != 20) {
		return 5;
	}
	int addr_grid = classify_addr_grid(&grid);
	if (addr_grid != 0) {
		return 60 + addr_grid;
	}
	int (*addr_rows)[][2] = &rows;
	if ((*addr_rows)[2][0] != 11 || (*addr_rows)[0][1] != 8) {
		return 7;
	}
	const int const_value = 21;
	const int* const_pointer = &const_value;
	int const_pointee = classify_const_pointee(&*const_pointer);
	if (const_pointee != 0) {
		return 80 + const_pointee;
	}
	int mutable_value = 21;
	int* const const_pointer_object = &mutable_value;
	int pointer_const = classify_const_pointee(&*const_pointer_object);
	if (pointer_const != 1) {
		return 90 + pointer_const;
	}
	const int const_grid[2][3] = {{1, 2, 3}, {4, 5, 6}};
	if ((*const_grid)[1] != 2 || (*const_grid)[2] != 3) {
		return 9;
	}
	long wide[2][2] = {{100L, 200L}, {300L, 400L}};
	if ((*wide)[1] != 200L) {
		return 10;
	}
	long (*wide_row)[2] = &(*wide);
	if (wide_row[1][0] != 300L) {
		return 11;
	}
	short matrix[2][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}};
	if ((*matrix)[3] != 4) {
		return 12;
	}
	short (*matrix_row)[4] = &(*matrix);
	if (matrix_row[1][0] != 5) {
		return 13;
	}
	Cell cells[2][2] = {{{1, 'a'}, {2, 'b'}}, {{3, 'c'}, {4, 'd'}}};
	if ((*cells)[1].value != 2 || (*cells)[1].tag != 'b') {
		return 14;
	}
	Cell (*cell_row)[2] = &(*cells);
	if (cell_row[1][0].value != 3) {
		return 15;
	}
	PairGrid<int> pairs{{{8, 9}, {10, 11}}};
	if ((*pairs.cells)[0] != 8 || (*pairs.cells)[1] != 9) {
		return 16;
	}
	int (*pair_row)[2] = &(*pairs.cells);
	if (pair_row[1][0] != 10) {
		return 17;
	}
	int (*row_pointer)[3] = &grid[0];
	if ((*row_pointer)[2] != 3) {
		return 18;
	}
	int (*from_pointer)[3] = &(*row_pointer);
	if (from_pointer[1][0] != 4) {
		return 19;
	}
	int depth_value = 15;
	int* depth_pointer = &depth_value;
	int** depth_pointer_pointer = &depth_pointer;
	int* one_level = *depth_pointer_pointer;
	if (one_level != depth_pointer || *one_level != 15) {
		return 20;
	}
	Overloaded overloaded{40};
	if (*overloaded != 41) {
		return 21;
	}
	int* overloaded_address = &overloaded;
	if (overloaded_address != &overloaded.value || *overloaded_address != 40) {
		return 22;
	}
	int (*function_pointer)(int) = &id;
	if (function_pointer(4) != 4) {
		return 23;
	}
	return 0;
}
