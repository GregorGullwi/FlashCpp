// Test that operator() overload resolution correctly distinguishes struct types
// when the argument is passed as a reference to a different struct.
//
// This exercises the can_convert_type ref-to-non-ref path (OverloadResolution.h
// line ~392) through the operator() overload resolution machinery added by this
// PR. Without the type_index fix, both overloads would score ExactMatch for a
// Bar& argument, causing ambiguity or wrong selection.
//
// operator()(Foo x) → returns 10
// operator()(Bar x) → returns 20
// Called as dispatch(bar_ref) where bar_ref is Bar&.
//
// Correct: operator()(Bar) selected → 20.

struct Foo {
	int value;
	Foo() : value(0) {}
};

struct Bar {
	int value;
	Bar() : value(0) {}
};

struct Dispatch {
	int operator()(Foo x) { return 10; }
	int operator()(Bar x) { return 20; }
};

int main() {
	Bar b;
	Bar& bar_ref = b;
	Dispatch dispatch;
	return dispatch(bar_ref); // should select operator()(Bar) → 20
}
