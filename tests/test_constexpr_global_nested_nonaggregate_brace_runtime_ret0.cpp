struct Inner {
	int x;
	int y;

	constexpr Inner()
		: x(-100), y(-100) {}

	constexpr Inner(int a, int b)
		: x(a + 10), y(b + 20) {}
};

struct Outer {
	Inner inner;
	int z;
};

constexpr Outer globalOuter = {{1, 2}, 3};

static_assert(globalOuter.inner.x == 11);
static_assert(globalOuter.inner.y == 22);
static_assert(globalOuter.z == 3);

int main() {
	if (globalOuter.inner.x != 11)
		return 1;
	if (globalOuter.inner.y != 22)
		return 2;
	if (globalOuter.z != 3)
		return 3;
	return 0;
}
