// Test that can_convert_type rejects Bar& → Foo (different struct types)
// in the "from is reference, to is non-reference" path.
//
// Bug: OverloadResolution.h line ~392 compares from_resolved == to_resolved
// using only the Type enum. Since both Bar and Foo resolve to Type::Struct,
// the check incorrectly returns exact_match(). The type_index is not compared,
// unlike the ref→ref and value→ref sub-cases which were fixed by this PR.
//
// The only overload takes Foo by value, but the argument is Bar&.
// There is no valid conversion from Bar to Foo, so this must fail.

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
