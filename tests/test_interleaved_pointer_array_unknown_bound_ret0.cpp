// [dcl.init.aggr]: an array of unknown bound initialized by a brace-enclosed
// list of n clauses, n > 0, has n elements. Each clause initializes one
// int (*)[4]. A trailing comma does not add an element, and direct-list
// initialization uses the same aggregate rule.
int storage[4] = {7, 8, 9, 10};
int other[4] = {11, 12, 13, 14};
int (*cells[])[4] = {&storage, &other, &storage};
int (*trailing[])[4] = {&storage, &other,};
int (*with_null[])[4] = {&other, nullptr};
int (*direct[])[4]{&storage};

int main() {
	int local_storage[4] = {1, 2, 3, 4};
	int (*local_cells[])[4] = {&local_storage, &storage};
	int (*local_direct[])[4]{&local_storage};
	if (sizeof(cells) != 3 * sizeof(void*)) return 1;
	if (sizeof(trailing) != 2 * sizeof(void*)) return 2;
	if (sizeof(with_null) != 2 * sizeof(void*)) return 3;
	if (sizeof(direct) != sizeof(void*)) return 4;
	if (sizeof(local_cells) != 2 * sizeof(void*)) return 5;
	if (sizeof(local_direct) != sizeof(void*)) return 6;
	if (cells[0][0][0] != 7) return 7;
	if (cells[1][0][1] != 12) return 8;
	if (cells[2][0][3] != 10) return 9;
	if (trailing[1][0][2] != 13) return 10;
	if (with_null[0][0][3] != 14) return 11;
	if (with_null[1] != nullptr) return 12;
	if (direct[0][0][0] != 7) return 13;
	if (local_cells[0][0][3] != 4) return 14;
	if (local_cells[1][0][0] != 7) return 15;
	if (local_direct[0][0][1] != 2) return 16;
	return 0;
}
