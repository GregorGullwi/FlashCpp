// Regression: sizeof/alignof on pointer-to-array type-ids must parse through
// the abstract-declarator path (C++20 [dcl.name], [dcl.ptr]/1).
// sizeof(int(*)[3]) is a pointer (8 bytes), not an array of 3 ints;
// sizeof(int[2][4]) stays an array object (16-byte row, 32 bytes total);
// the named-declarator form int(*p)[3] with sizeof(*p) must not regress.

struct Point {
	int x;
	int y;
};

struct Holder {
	int (*cursor)[3];
	long (*grid)[4];
};

int main() {
	// Pointer-to-array type-ids are pointers: 8 bytes on x64.
	if (sizeof(int(*)[3]) != 8) {
		return 1;
	}
	if (sizeof(int(*)[2][4]) != 8) {
		return 2;
	}
	if (sizeof(Point(*)[4]) != 8) {
		return 3;
	}

	// Array type-ids keep their object sizes.
	if (sizeof(int[2][4]) != 32) {
		return 4;
	}
	if (sizeof(Point[2]) != 16) {
		return 5;
	}

	// CV forms of the pointer-to-array type-id parse and stay pointers.
	if (sizeof(const int(*)[3]) != 8) {
		return 6;
	}
	if (sizeof(int(*const)[3]) != 8) {
		return 7;
	}

	// alignof agrees with the pointer alignment.
	if (alignof(int(*)[3]) != 8) {
		return 8;
	}

	// Named-declarator form: sizeof(*p) dereferences to the pointee array.
	int arr[3] = {7, 8, 9};
	int (*p)[3] = &arr;
	if (sizeof(*p) != 12) {
		return 9;
	}
	if (sizeof(p) != sizeof(int(*)[3])) {
		return 10;
	}

	// long long is 8 bytes under both LLP64 (Windows) and LP64 (Linux),
	// so the pointee array size below holds on every supported data model.
	long long row[4] = {1LL, 2LL, 3LL, 4LL};
	long long (*g)[4] = &row;
	if (sizeof(*g) != 32) {
		return 11;
	}

	// Struct members holding pointer-to-array objects follow the same sizing.
	if (sizeof(Holder) != 16) {
		return 12;
	}
	if (sizeof(long(*)[4]) != sizeof(g)) {
		return 13;
	}

	Point points[2] = {{1, 2}, {3, 4}};
	Point (*pp)[2] = &points;
	if (sizeof(*pp) != 16) {
		return 14;
	}

	// alignof on any pointer/reference form is 8 regardless of the pointee
	// (guards the stale size_in_bits alignment path).
	if (alignof(int*) != 8) {
		return 15;
	}
	if (alignof(Point(*)[4]) != 8) {
		return 16;
	}
	if (alignof(long&) != 8) {
		return 17;
	}

	return 0;
}
