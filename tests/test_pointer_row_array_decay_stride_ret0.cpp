// Decaying T* rows[M][N] must keep the element pointer inside the row array,
// so the resulting pointer-to-row advances by a whole row.

static int a = 1, b = 2, c = 3, d = 4, e = 5, f = 6;
using RowPointer = int* (*)[3];

int main() {
	int* rows[2][3] = {{&a, &b, &c}, {&d, &e, &f}};
	auto decayed = rows;
	static_assert(__is_same(decltype(decayed), RowPointer));
	if (decayed[1][2] != &f) {
		return 1;
	}
	*decayed[0][1] = 20;
	if (b != 20) {
		return 2;
	}
	if (*decayed[1][0] != 4) {
		return 3;
	}
	return 0;
}
