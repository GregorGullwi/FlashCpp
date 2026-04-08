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

constexpr int testSizeof() {
	int a = 1;
	int b = 2;
	Pair p{a, 4.0};
	int* ptr = &a;

	return sizeof(a + b)
		+ sizeof(makeInt())
		+ sizeof(p.x)
		+ sizeof(true ? a : b)
		+ sizeof(static_cast<short>(b))
		+ sizeof(*ptr);
}

constexpr int testAlignof() {
	int a = 1;
	int b = 2;
	Pair p{a, 4.0};
	int* ptr = &a;

	return alignof(a + b)
		+ alignof(makeDouble())
		+ alignof(p.y)
		+ alignof(true ? a : b)
		+ alignof(static_cast<short>(b))
		+ alignof(*ptr);
}

static_assert(testSizeof() == 22);
static_assert(testAlignof() == 30);

int main() {
	return 0;
}
