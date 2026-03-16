// Test that overload resolution correctly distinguishes Bar& → Foo& vs Bar& → Bar&
// in the "both are references" path of can_convert_type.
//
// This path was fixed by this PR (type_index check at OverloadResolution.h ~line 323).
// This test serves as a regression guard: without the fix, both overloads score
// ExactMatch because Type::Struct == Type::Struct is always true regardless of
// which struct the reference refers to.
//
// Overloads:
//   g(Foo&) → 10
//   g(Bar&) → 20
// Call: g(b) where b is Bar (lvalue → Bar&).
//
// Correct: g(Bar&) selected → 20.
// Buggy:   both score ExactMatch → ambiguous or first-declared wins → 10.

struct Foo {
	int value;
	Foo() : value(0) {}
};

struct Bar {
	int value;
	Bar() : value(0) {}
};

int g(Foo& x) { return 10; }
int g(Bar& x) { return 20; }

int main() {
	Bar b;
	return g(b); // should select g(Bar&) → 20
}
