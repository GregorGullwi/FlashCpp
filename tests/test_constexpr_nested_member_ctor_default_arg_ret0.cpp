struct Inner {
	int first;
	int second;

	constexpr Inner(int a, int b = 9) : first(a), second(b) {}
};

struct Outer {
	Inner inner;

	constexpr Outer(int a) : inner(a) {}
};

constexpr int read_nested_second() {
	Outer value(4);
	return value.inner.second;
}

static_assert(read_nested_second() == 9);

int main() {
	return read_nested_second() == 9 ? 0 : 1;
}
