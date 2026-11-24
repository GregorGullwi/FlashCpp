// Pointer-to-row ordered declarators keep the pointer as the object: the row
// extent belongs to the pointee, so sizeof of the array counts pointers. Mixing
// int and long long rows keeps a builtin-element guess from passing.
struct S {
	int (*p)[4];
};

struct RowHolder {
	long long (*row)[5];
};

int main() {
	int (*rows[2])[3];
	long long (*wide[2])[5];
	int (*unbounded[2])[];
	if (sizeof(rows) != 16) {
		return 1;
	}
	if (sizeof(wide) != 16) {
		return 2;
	}
	// An unknown pointee bound is fine: the element is a pointer, not a row.
	if (sizeof(unbounded) != 16) {
		return 3;
	}
	if (sizeof(**rows) != 12) {
		return 4;
	}
	if (sizeof(**wide) != 40) {
		return 5;
	}
	if (sizeof(S) != 8) {
		return 6;
	}
	if (sizeof(RowHolder) != 8) {
		return 7;
	}
	return 31;
}
