// Test: Two-level nested braced initializers
// Tests that nested braced initializers work recursively

struct Inner {
	int a;
	int b;
};

struct Middle {
	Inner inner;
	int c;
};

struct Outer {
	Middle middle;
	int d;
};

struct ReallyFar {
	Outer outer;
	int e = 50;
};

int main() {
	// Test two-level nested braced initialization
	Outer oo = { .middle = { .inner = { .a = 10, .b = 20 }, .c = 30 }, .d = 40 };
	Outer o = { { { 10, 20 }, 30 }, 40 };
	ReallyFar rf = { .outer = o };
	// Expected: 10 + 20 + 30 + 40 = 100
	return oo.middle.inner.a + o.middle.inner.b + o.middle.c + o.d;
}
