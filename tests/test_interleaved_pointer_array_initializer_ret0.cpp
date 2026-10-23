// [dcl.init.aggr]: an array of pointers to arrays is initialized one outermost
// element at a time. The element type stays int (*)[4]. A short list
// value-initializes the remaining pointers.
int storage[4] = {7, 8, 9, 10};
int other[4] = {11, 12, 13, 14};
int (*cells[3])[4] = {&storage, &other, &storage};
int (*partial[2])[4] = {&other};

int main() {
	int local_storage[4] = {1, 2, 3, 4};
	int (*local_cells[2])[4] = {&local_storage, &storage};
	int (*local_partial[2])[4] = {&local_storage};
	if (sizeof(cells) != 3 * sizeof(void*)) return 1;
	if (sizeof(local_cells) != 2 * sizeof(void*)) return 2;
	if (cells[0][0][0] != 7) return 3;
	if (cells[1][0][1] != 12) return 4;
	if (cells[2][0][3] != 10) return 5;
	if (partial[0][0][2] != 13) return 6;
	if (partial[1] != nullptr) return 7;
	if (local_cells[0][0][3] != 4) return 8;
	if (local_cells[1][0][0] != 7) return 9;
	if (local_partial[0][0][1] != 2) return 10;
	if (local_partial[1] != nullptr) return 11;
	return 0;
}
