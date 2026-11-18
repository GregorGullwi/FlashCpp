// A T* rows[][N] parameter decays to a pointer-to-row with the row's element
// pointer preserved, matching a decayed T* rows[M][N] argument.

static int a = 1, b = 2, c = 3, d = 4, e = 5, f = 6;
using RowPointer = int* (*)[3];

int read_unsized(int* rows[][3]) {
	if (rows[1][2] != &f) {
		return 1;
	}
	*rows[0][1] = 21;
	return 0;
}

int read_sized(int* rows[2][3]) {
	if (rows[1][0] != &d) {
		return 2;
	}
	return 0;
}

int main() {
	int* rows[2][3] = {{&a, &b, &c}, {&d, &e, &f}};
	RowPointer decayed = rows;
	if (read_unsized(rows) != 0 || b != 21) {
		return 3;
	}
	if (read_sized(decayed) != 0) {
		return 4;
	}
	return 0;
}
