// Foo has no constructor taking Bar, so f(b) should be rejected.

struct Foo {
	int value;
	Foo() : value(0) {}
};

struct Bar {
	int value;
	Bar() : value(0) {}
};

int f(Foo x) { return x.value; }

int main() {
	Bar b;
	b.value = 42;
	return f(b); // no conversion from Bar to Foo — should fail
}
