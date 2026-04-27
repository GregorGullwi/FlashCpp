// Function overload sets from a namespace and its inline child must still
// be merged into a single overload set for qualified lookup.
namespace A {
	int foo(int) { return 0; }
	inline namespace B {
		int foo(double) { return 1; }
	}
}
int main() {
	return A::foo(1); // selects foo(int) -> returns 0
}
