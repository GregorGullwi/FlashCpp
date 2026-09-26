// Regression: pointer-to-array data members retain their pointee bounds after
// class-template instantiation ([dcl.ptr]/1, [expr.sub], [dcl.array]/1).
template <typename T, int Rows, int Columns>
struct Box {
	T (*cells)[Rows][Columns];
};

int int_rows[2][3] = {{10, 11, 12}, {13, 14, 15}};
long long_rows[2][4] = {{20, 21, 22, 23}, {24, 25, 26, 27}};

int main() {
	Box<int, 2, 3> ints;
	if (sizeof(ints) != sizeof(void*)) return 7;
	ints.cells = &int_rows;
	if ((*ints.cells)[0][2] != 12) {
		return 1;
	}
	if ((*ints.cells)[1][1] != 14) {
		return 2;
	}
	(*ints.cells)[1][2] = 42;
	if (int_rows[1][2] != 42) {
		return 3;
	}

	Box<long, 2, 4> longs;
	if (sizeof(longs) != sizeof(void*)) return 8;
	longs.cells = &long_rows;
	if ((*longs.cells)[0][3] != 23L) {
		return 4;
	}
	if ((*longs.cells)[1][1] != 25L) {
		return 5;
	}
	(*longs.cells)[1][3] = 42L;
	if (long_rows[1][3] != 42L) {
		return 6;
	}

	return 0;
}
