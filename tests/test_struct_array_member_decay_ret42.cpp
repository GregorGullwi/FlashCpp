// Regression: array-to-pointer decay of a struct array member in a local
// variable initializer. The member access produces the member's storage
// address, but the initializer lowering used to dereference it, so the pointer
// held the array's first bytes reinterpreted as an address and crashed.

struct Inner {
	int values[2];
};

struct Outer {
	int tag;
	Inner inner;
	int mat[2][3];
};

struct Tagged {
	int tag;
	int values[4];
};

void consume(int* data, int count) {
	(void)data;
	(void)count;
}

int main() {
	Tagged tagged = {1, {2, 3, 4, 5}};
	int* p = tagged.values;
	if (p[0] != 2 || p[3] != 5) return 1;

	const int* cp = tagged.values;
	if (cp[1] != 3) return 2;

	void* vp = tagged.values;
	if (vp == nullptr) return 3;

	Tagged arr[2] = {{6, {7, 8, 9, 10}}, {11, {12, 13, 14, 15}}};
	int* element = arr[1].values;
	if (element[0] != 12 || element[3] != 15) return 4;

	int index = 2;
	int* indexed = arr[0].values;
	if (indexed[index] != 9) return 5;

	consume(arr[0].values, 4);

	Outer outer = {16, {{17, 18}}, {{19, 20, 21}, {22, 23, 24}}};
	int* nested = outer.inner.values;
	if (nested[0] != 17 || nested[1] != 18) return 6;

	int (*row)[3] = outer.mat;
	if (row[0][2] != 21 || row[1][0] != 22) return 7;

	int local[3] = {25, 26, 27};
	int* plain = local;
	if (plain[2] != 27) return 8;

	return 42;
}
