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

int main() {
	// Test two-level nested braced initialization
	// Outer o = { { { .a = 10, .b = 20 }, .c = 30 }, .d = 40 };
	Outer o = { { { 10, 20 }, 30 }, 40 };
	
	// Expected: 10 + 20 + 30 + 40 = 100
	return o.middle.inner.a + o.middle.inner.b + o.middle.c + o.d;
}
