// C++20 [dcl.ref]: a reference to an array holds the array's address, not an
// array value. An interleaved declarator such as int (* (&)[N])[M] has no flat
// projection, so identifier lowering must read its outer array from the ordered
// spine instead of dereferencing the reference as a value.
int storage[4] = {7, 8, 9, 10};
int other[4] = {1, 2, 3, 4};
int (*cells[2])[4] = {&storage, &other};

int sum_first_and_last(int (* (&rows)[2])[4]) {
	return (*rows[0])[0] + (*rows[1])[3];
}

int main() {
	int (* (&global_ref)[2])[4] = cells;
	if ((*global_ref[0])[1] != 8) return 1;
	if ((*global_ref[1])[2] != 3) return 2;

	int (*local_cells[2])[4] = {&storage, &other};
	int (*second_cells[2])[4] = {&other, &storage};
	int (* (&local_ref)[2])[4] = local_cells;
	if ((*local_ref[0])[3] != 10) return 3;
	if ((*local_ref[1])[0] != 1) return 4;

	// The local array frame slot must span both pointer-sized elements without
	// overwriting the adjacent ordered array.
	if ((*second_cells[0])[3] != 4) return 5;
	if ((*second_cells[1])[0] != 7) return 6;

	if (sum_first_and_last(local_cells) != 11) return 7;
	if (sum_first_and_last(global_ref) != 11) return 8;
	return 42;
}
