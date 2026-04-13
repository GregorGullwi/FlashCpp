constexpr int makeInt() {
	return 42;
}

constexpr double makeDouble() {
	return 3.5;
}

struct Pair {
	int x;
	double y;
};

struct MatrixBox {
	int matrix[2][3];
	int data[3];
};

constexpr int testSizeof() {
	int a = 1;
	int b = 2;
	Pair p{a, 4.0};
	int* ptr = &a;
	int arr[3] = {1, 2, 3};
	MatrixBox box{{{1, 2, 3}, {4, 5, 6}}, {7, 8, 9}};

	return sizeof(a + b)
		+ sizeof(makeInt())
		+ sizeof(p.x)
		+ sizeof(true ? a : b)
		+ sizeof(static_cast<short>(b))
		+ sizeof(*ptr)
		+ sizeof(arr[1])
		+ sizeof(box.data[2])
		+ sizeof(box.matrix[0]);
}

constexpr int testAlignof() {
	int a = 1;
	int b = 2;
	Pair p{a, 4.0};
	int* ptr = &a;
	int arr[3] = {1, 2, 3};
	MatrixBox box{{{1, 2, 3}, {4, 5, 6}}, {7, 8, 9}};

	return alignof(a + b)
		+ alignof(makeDouble())
		+ alignof(p.y)
		+ alignof(true ? a : b)
		+ alignof(static_cast<short>(b))
		+ alignof(*ptr)
		+ alignof(arr[1])
		+ alignof(box.data[2])
		+ alignof(box.matrix[0]);
}

static_assert(testSizeof() == 42);
static_assert(testAlignof() == 42);

int main() {
	return 0;
}
