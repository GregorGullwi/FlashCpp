// Regression: named multi-bound pointer-to-array declarators parse and bind
// every bound inside the pointer in all declarator contexts
// (C++20 [dcl.ptr]/1, [dcl.array]/1).
// long (*t)[2][4] declares t as a scalar pointer whose pointee is long[2][4];
// sizeof(t) is the pointer size and sizeof(*t) spans both bounds. Size
// expectations are derived from sizeof(long): LLP64 (Windows) long is 4
// bytes, LP64 (Linux) long is 8 bytes.

long g_storage[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}};
long (*g_table)[2][4];

struct Holder {
	long (*member)[2][4];
	int tag;
};

struct Point2D {
	int x;
	int y;
};

Point2D pts[2][2] = {{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}};

long take(long (*param)[2][4]) {
	return (*param)[1][2];
}

// Function returning a pointer to a two-dimensional array.
long (*make(long (*src)[2][4]))[2][4] {
	return src;
}

int main() {
	if (sizeof(g_table) != 8) {
		return 1;
	}
	if (sizeof(*g_table) != sizeof(long) * 2 * 4) {
		return 2;
	}
	if (sizeof(Holder) != 16) {
		return 3;
	}

	g_table = &g_storage;
	if ((*g_table)[1][3] != 7L) {
		return 4;
	}

	long (*local)[2][4] = &g_storage;
	if ((*local)[0][5] != 5L) {
		return 5;
	}

	Holder h;
	h.member = local;
	if ((*h.member)[1][0] != 4L) {
		return 6;
	}
	if (take(h.member) != 6L) {
		return 7;
	}

	long (*returned)[2][4] = make(local);
	if ((*returned)[0][0] != 0L) {
		return 8;
	}

	// Multi-bound declarators over a struct element type keep scalar storage.
	// NOTE: uses file-scope storage; local nested-brace initialization of
	// multidimensional struct arrays has a separate known bug.
	Point2D (*ppts)[2][2] = &pts;
	if (sizeof(ppts) != 8) {
		return 9;
	}
	// Point2D is int-based, so this bound-span check is platform stable.
	if (sizeof(*ppts) != 32) {
		return 10;
	}
	if ((*ppts)[1][1].y != 8) {
		return 11;
	}
	return 0;
}
