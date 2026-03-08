struct Inner {
	int seed;
	int value = 42;

	constexpr Inner(int s) : seed(s) {}
};

struct Outer {
	Inner inner;

	constexpr Outer(int s) : inner(s) {}
};

constexpr Outer outer(7);
constexpr int result = outer.inner.value;

int main() {
	return result;
}
