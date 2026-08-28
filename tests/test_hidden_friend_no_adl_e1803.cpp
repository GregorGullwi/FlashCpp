// A friend function defined inside a class body is a "hidden friend" (C++20 [basic.lookup.argdep]).
// It is NOT introduced into the enclosing namespace for ordinary unqualified lookup.
// It can only be found via ADL when at least one argument has the associated class type.
//
// This test verifies that calling a hidden friend without an argument of the associated
// class type is correctly rejected as an error.
struct Widget {
	int value;
 // Hidden friend: no Widget-typed parameter, so ADL can never find it either.
	friend int make_widget_value() { return 42; }
};
int main() {
 // Should fail: make_widget_value is a hidden friend; ordinary unqualified lookup
 // cannot find it, and ADL does not apply (no argument of type Widget).
	return make_widget_value();
}
