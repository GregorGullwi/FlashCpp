// Test that can_convert_type rejects Bar& → const Foo& (different struct types)
// in the "both are references" path.
//
// Distinct struct types must not be treated as convertible just because both
// are represented by the same broad struct category.
//
// The call to f is made from a function whose parameter is Bar&, exercising
// the reference-to-reference conversion path. There is no valid conversion,
// so this must fail.

struct Foo {
	int value;
	Foo() : value(0) {}
};

struct Bar {
	int value;
	Bar() : value(0) {}
};

int f(const Foo& x) { return x.value; }

int call_with_bar_reference(Bar& b) {
	return f(b);
}

int main() {
	Bar b;
	b.value = 42;
	return call_with_bar_reference(b);
}
