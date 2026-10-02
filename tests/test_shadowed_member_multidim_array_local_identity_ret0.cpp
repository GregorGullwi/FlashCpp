// Regression: member-array accesses must retain the resolved local object's
// identity when another object with the same name is in a different scope.
struct Matrix {
	int values[2][2];
};

struct MatrixOwner {
	int values[2][2];
	Matrix nested;
};

int main() {
	{
		MatrixOwner owner = {{{1, 2}, {3, 4}}, {{{9, 10}, {11, 12}}}};
		(void)owner;
	}
	{
		MatrixOwner owner = {{{5, 6}, {7, 8}}, {{{13, 14}, {15, 16}}}};
		if (owner.values[1][1] != 8) return 1;
		// The nested dotted path goes through a composite member before reaching
		// the array and exercises member-array base propagation through temps.
		if (owner.nested.values[1][1] != 16) return 2;
	}
	return 0;
}

