// Regression test: struct member brace-init inside a constexpr constructor must
// try constructor resolution before aggregate fallback when the member type is
// non-aggregate.

struct Inner {
	int x;
	int y;
	constexpr Inner(int a, int b) : x(a), y(b) {}
};

struct Outer {
	Inner inner;
	int sum;
	constexpr Outer(int a, int b) : inner{a, b}, sum(a + b) {}
};

constexpr Outer g_outer(3, 7);
static_assert(g_outer.inner.x == 3);
static_assert(g_outer.inner.y == 7);
static_assert(g_outer.sum == 10);

constexpr int local_sum() {
	Outer o(4, 6);
	return o.inner.x + o.inner.y + o.sum;
}

static_assert(local_sum() == 20);

int main() {
	return (g_outer.inner.x == 3 && g_outer.inner.y == 7 && g_outer.sum == 10 &&
			local_sum() == 20)
		? 0
		: 1;
}
