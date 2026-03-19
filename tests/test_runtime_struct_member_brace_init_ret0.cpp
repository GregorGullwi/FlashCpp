// Test: runtime struct aggregate brace-init in constructor initializer list.
// e.g., inner{a, b} for struct Inner { int a; int b; };

struct Inner {
	int a;
	int b;
};

struct Outer {
	Inner inner;
	Outer(int x, int y) : inner{x, y} {}
};

struct PartialOuter {
	Inner inner;
	PartialOuter(int x) : inner{x} {}
};

int main() {
	Outer o(10, 20);
	if (o.inner.a != 10) return 1;
	if (o.inner.b != 20) return 2;

	// Single-element fills first, second zero
	PartialOuter p(5);
	if (p.inner.a != 5) return 3;
	if (p.inner.b != 0) return 4;

	return 0;
}
