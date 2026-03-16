// Regression test: callable-object operator() is pre-resolved by semantic
// analysis rather than codegen.  The callable has two operator() overloads
// with different arities; sema must select the one whose parameter count
// matches the argument count at the call site.

struct Counter {
	int base;

	// One-argument overload: base + x
	int operator()(int x) {
		return base + x;
	}

	// Two-argument overload: base + x + y
	int operator()(int x, int y) {
		return base + x + y;
	}
};

int main() {
	Counter c;
	c.base = 10;

	// sema must pick the 1-arg overload → 10 + 5 = 15
	int a = c(5);
	// sema must pick the 2-arg overload → 10 + 3 + 4 = 17
	int b = c(3, 4);

	return (a == 15 && b == 17) ? 0 : 1;
}
