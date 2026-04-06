// Brace-init constructor overload resolution should still reject ambiguous calls
// after the parser stops doing the authoritative overload selection itself.

struct Foo {
	Foo(int, double) {}
	Foo(double, int) {}
};

int main() {
	Foo value{1, 2}; // ambiguous: neither overload is better
	return 0;
}
