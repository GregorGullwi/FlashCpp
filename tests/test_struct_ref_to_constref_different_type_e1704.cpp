// Test that can_convert_type rejects Bar& → const Foo& (different struct types)
// in the "both are references" path.
//
// This PR added a type_index check for the ref→ref case (OverloadResolution.h
// lines 323-327), so Bar& → const Foo& should now correctly return no_match.
// Without that fix, Type::Struct == Type::Struct would incorrectly match.
//
// This test verifies the fix is working: the only overload takes const Foo&,
// but the argument is Bar&. There is no valid conversion, so this must fail.

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
	Bar b;
	b.value = 42;
	return f(b); // no conversion from Bar to const Foo& — should fail
}
