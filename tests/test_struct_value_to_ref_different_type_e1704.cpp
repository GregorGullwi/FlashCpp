// Test that can_convert_type rejects Bar → const Foo& (different struct types)
// in the "from is non-reference, to is reference" path.
//
// Distinct struct types must not be treated as convertible just because both
// are represented by the same broad struct category.
//
// The only overload takes const Foo&, but the argument is a Bar prvalue.
// There is no valid conversion from Bar to const Foo&, so this must fail.

struct Foo {
	int value;
	Foo() : value(0) {}
};

struct Bar {
	int value;
	Bar() : value(0) {}
};

int f(const Foo& x) { return x.value; }

int main() {
	return f(Bar{}); // no conversion from a Bar temporary to const Foo& — should fail
}
