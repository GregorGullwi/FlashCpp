// Test that ambiguous constructor overloads are rejected.
// Two constructors with cross-wise parameter types: neither is strictly better
// for arguments (int, int). A conforming compiler must reject this as ambiguous.
//
// Constructor 1: Foo(int, double)  -> arg 0 exact, arg 1 conversion
// Constructor 2: Foo(double, int)  -> arg 0 conversion, arg 1 exact
// Call:           Foo(1, 2)        -> neither constructor is strictly better

struct Foo {
	int value;
	Foo(int a, double b) : value(10) {}
	Foo(double a, int b) : value(20) {}
};

int main() {
	Foo f(1, 2); // ambiguous: should fail to compile
	return f.value;
}
