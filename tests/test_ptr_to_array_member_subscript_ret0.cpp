// Regression: indexing through a pointer-to-array struct member
// (C++20 [dcl.ptr]/1, [expr.sub], [dcl.array]/1).
// h.cursor[0][i] must lower as element access through the member's pointer
// value with row-major strides over the pointee bounds; the member itself is
// scalar pointer storage ([expr.sizeof]).
//
// NOTE: the same access through a class-template-instantiated member
// (Box<int>::cells) still crashes; tracked separately.

long rows[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}};
int int_rows[2][3] = {{10, 11, 12}, {13, 14, 15}};

struct Grid {
	long (*cursor)[2][4];
};

struct IntBox {
	int (*cells)[2][3];
};

int main() {
	Grid g;
	g.cursor = &rows;

	// Flattened row-major indexing through the member pointer.
	if ((*g.cursor)[0][5] != 5L) {
		return 1;
	}
	if ((*g.cursor)[1][0] != 4L) {
		return 2;
	}

	// Writes through the member pointer land in the backing array.
	(*g.cursor)[0][1] = 42L;
	if (rows[0][1] != 42L) {
		return 3;
	}

	// Member holding a pointer to an array of native elements.
	IntBox b;
	b.cells = &int_rows;
	if ((*b.cells)[0][2] != 12) {
		return 4;
	}
	if ((*b.cells)[1][1] != 14) {
		return 5;
	}

	// sizeof of the dereferenced member spans every recorded bound.
	// Derived from sizeof(long): LLP64 (Windows) long is 4 bytes, LP64
	// (Linux) long is 8 bytes.
	if (sizeof(*g.cursor) != sizeof(long) * 2 * 4) {
		return 6;
	}
	return 0;
}

