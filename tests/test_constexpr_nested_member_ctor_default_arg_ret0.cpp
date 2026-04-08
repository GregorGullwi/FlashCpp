struct Inner {
	int first;
	int second;

	constexpr Inner(int a, int b = 9) : first(a), second(b) {}
};

struct Outer {
	Inner inner;

	constexpr Outer(int a) : inner(a) {}
};

constexpr Outer g_outer(4);

constexpr int read_nested_second() {
	return g_outer.inner.second;
}

static_assert(g_outer.inner.second == 9);
static_assert(read_nested_second() == 9);
constexpr int extracted = g_outer.inner.second;

int main() {
	return extracted == 9 ? 0 : 1;
}
