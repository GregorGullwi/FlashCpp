// Regression test: aggregate initialization of Outer with a nested non-aggregate
// member (Inner has user-defined constructors but no default constructor).
// Inner{1, 2} must call Inner(int, int), and Outer is aggregate-initialized.
// Expected return: 1 + 2 + 3 = 6.

struct Inner {
	int x;
	int y;
	Inner(int a, int b) : x(a), y(b) {}
	// No default constructor — Inner is NOT an aggregate
};

struct Outer {
	Inner inner;
	int z;
	// No user-declared constructors — Outer IS an aggregate
	// But its implicit default constructor is deleted (Inner has no default ctor)
};

int main() {
	Outer o = {{1, 2}, 3};
	return o.inner.x + o.inner.y + o.z;
}
