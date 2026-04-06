// Empty brace-init must diagnose a missing default constructor after constructor
// overload selection moved out of the parser.

struct Foo {
	Foo(int) {}
};

int main() {
	Foo value{};
	return 0;
}
