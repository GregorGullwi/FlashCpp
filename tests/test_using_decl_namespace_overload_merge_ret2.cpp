// C++20 [namespace.udecl]: a using-declaration at namespace scope introduces the
// named declarations as members of that namespace, so they join the overload set
// formed by the namespace's own declarations rather than being hidden by them.
//
// Here B::f(double) is declared before `using A::f;`. A call to f(1) inside B must
// consider both and select the exact match A::f(int), returning 2. If the
// using-declared overload is invisible, f(double) is selected instead and the
// program returns 101.

namespace A {
	int f(int x) { return x + 1; }
}

namespace B {
	int f(double x) { return static_cast<int>(x) + 100; }
	using A::f;

	int call() { return f(1); }
}

int main() {
	return B::call();
}
