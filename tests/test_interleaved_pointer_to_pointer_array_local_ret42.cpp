// A local declared as int (**p)[4] is a scalar pointer to the first
// pointer-to-array element of an int (*[N])[M] array. The declaration retains
// array extents for its pointee, but subscripting the variable must load the
// stored pointer, not index the variable's stack slot as an array object.
int storage[4] = {7, 8, 9, 10};
int other[4] = {1, 2, 3, 4};
int (*cells[2])[4] = {&storage, &other};

int main() {
	int (**rows)[4] = cells;
	if ((*rows[0])[1] != 8) return 1;
	if ((*rows[1])[2] != 3) return 2;

	int (**local_rows)[4] = cells;
	if ((*local_rows[1])[3] != 4) return 3;
	return 42;
}
