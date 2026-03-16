// Test that overload resolution correctly distinguishes Bar& → Foo vs Bar& → Bar
// in the "from is reference, to is non-reference" path of can_convert_type.
//
// Bug: OverloadResolution.h ~line 392 compares from_resolved == to_resolved using
// only the Type enum.  Since both Bar& and Foo resolve to Type::Struct, the check
// `from_resolved == to_resolved` is always true for any two struct types, so
// can_convert_type incorrectly returns exact_match() for Bar& → Foo.
// The type_index is not compared, unlike the other two reference sub-cases
// (ref→ref at line 323 and value→ref at line 349) which were fixed by this PR.
//
// Overloads:
//   f(Foo) → 10
//   f(Bar) → 20
// Call: f(bar_ref) where bar_ref is Bar&.
//
// Correct: f(Bar) selected → 20.
// Buggy:   both score ExactMatch → ambiguous or first-declared wins → 10.

struct Foo {
	int value;
	Foo() : value(0) {}
};

struct Bar {
	int value;
	Bar() : value(0) {}
};

int f(Foo x) { return 10; }
int f(Bar x) { return 20; }

int main() {
	Bar b;
	Bar& bar_ref = b;
	return f(bar_ref); // should select f(Bar) → 20
}
