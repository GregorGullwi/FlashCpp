// Test: copy constructor with a trailing default argument.
// Foo(const Foo&, int extra = 0) should be recognized as a copy constructor.
// This validates the unified findSameTypeConstructorCore handles default args.

struct Foo {
	int val;
	Foo(int v) : val(v) {}
	Foo(const Foo& other, int extra = 0) : val(other.val + extra) {}
};

int main() {
	Foo a(42);
	Foo b(a);       // calls Foo(const Foo&, int=0) => val == 42
	Foo c(a, 8);    // calls Foo(const Foo&, int)   => val == 50
	return b.val + c.val - 92;  // 42 + 50 - 92 == 0
}
