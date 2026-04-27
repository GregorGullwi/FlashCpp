// Qualified lookup into a namespace whose inline child also declares the
// same non-function name must be diagnosed as ambiguous (C++20 [namespace.qual]).
namespace A {
	int x = 1;
	inline namespace B {
		int x = 2;
	}
}
int main() {
	return A::x; // ambiguous: both A::x and A::B::x are visible
}
