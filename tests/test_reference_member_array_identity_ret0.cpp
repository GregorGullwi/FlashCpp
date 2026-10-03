// A struct member that is a reference to an array stores the referenced array's
// address, not inline elements. Aggregate and constructor binding must both
// bind the reference, and subscripting through it must reach the referenced
// array for reads and writes.
struct RefArray {
	int (&ref)[3];
};

struct RefMatrix {
	int (&ref)[2][2];
};

struct BoundRefArray {
	int (&ref)[3];
	BoundRefArray(int (&r)[3]) : ref(r) {}
};

int main() {
	int values[3] = {1, 2, 3};
	RefArray holder{values};
	if (holder.ref[1] != 2) return 1;
	holder.ref[1] = 20;
	if (values[1] != 20) return 2;

	values[1] = 2;
	BoundRefArray bound(values);
	if (bound.ref[2] != 3) return 3;
	bound.ref[2] = 30;
	if (values[2] != 30) return 4;

	int matrix[2][2] = {{1, 2}, {3, 4}};
	RefMatrix matrix_holder{matrix};
	if (matrix_holder.ref[1][1] != 4) return 5;
	matrix_holder.ref[0][0] = 10;
	if (matrix[0][0] != 10) return 6;
	return 0;
}
