// [stmt.ranged]: an array range visits one outermost element. The element
// type of int (*cells[N])[4] is int (*)[4], including a bound completed
// from a brace list. A reference loop variable names that element.
int storage[4] = {7, 8, 9, 10};
int other[4] = {1, 2, 3, 4};

int main() {
	int local_other[4] = {11, 12, 13, 14};
	int (*cells[2])[4] = {&storage, &other};
	int (*local_cells[2])[4] = {&local_other, &storage};
	int (*completed[])[4] = {&other, &storage};
	int sum = 0;
	for (int (*row)[4] : cells) {
		sum += (*row)[0];
	}
	if (sum != 8) return 1;
	int ref_sum = 0;
	for (int (*&row_ref)[4] : cells) {
		ref_sum += (*row_ref)[1];
	}
	if (ref_sum != 10) return 2;
	int auto_sum = 0;
	for (auto row_auto : local_cells) {
		auto_sum += (*row_auto)[2];
	}
	if (auto_sum != 22) return 3;
	int completed_sum = 0;
	for (int (*row_done)[4] : completed) {
		completed_sum += (*row_done)[3];
	}
	if (completed_sum != 14) return 4;
	return 0;
}
