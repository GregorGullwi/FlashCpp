// Regression test: nested brace-init must try constructor resolution before
// aggregate fallback for non-aggregate struct members in constexpr paths.

struct Inner {
	int x;
	int y;
	constexpr Inner() : x(-100), y(-100) {}
	constexpr Inner(int a, int b) : x(a), y(b) {}
};

struct Outer {
	Inner inner;
	int z;
};

constexpr int outer_sum() {
	Outer o = {{1, 2}, 3};
	return o.inner.x + o.inner.y + o.z;
}

constexpr int array_sum() {
	Inner arr[] = {{4, 5}, {6, 7}};
	return arr[0].x + arr[1].y;
}

static_assert(outer_sum() == 6);
static_assert(array_sum() == 11);

int main() {
	return 0;
}
