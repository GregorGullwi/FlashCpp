// An interleaved declarator whose outermost wrapper is an array owns inline
// storage even though its flat projection cannot describe the element type.
// Global and local arrays of pointers to arrays must allocate pointer-sized
// elements and feed the stepwise subscript lowering.
int storage[4] = {7, 8, 9, 10};
int (*cells[3])[4];

int main() {
	int (*local_cells[2])[4];
	cells[0] = &storage;
	cells[1] = &storage;
	cells[2] = &storage;
	local_cells[0] = &storage;
	local_cells[1] = &storage;
	if (sizeof(cells) != 3 * sizeof(void*)) return 1;
	if (sizeof(local_cells) != 2 * sizeof(void*)) return 2;
	if (cells[0][0][0] != 7) return 3;
	if (local_cells[1][0][1] != 8) return 4;
	return 42;
}
