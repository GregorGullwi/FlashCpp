// Regression test: C++20 aggregate initialization with base-class-only intermediate struct
// Bug: Top t(7) caused ConstructorCallOp missing resolved constructor for 'Middle$...'
// because Middle was a C++20 aggregate (no user-declared ctors, no direct members,
// but with a base class).

template<typename T>
struct Base {
	T val;
	Base(T v) : val(v) {}
};

// C++20 aggregate: no user-declared ctor, no direct members, but has base class
template<typename T>
struct Middle : Base<T> {};

// Explicit ctor that initializes Middle via base-init: Middle<T>(v)
template<typename T>
struct Top : Middle<T> {
	Top(T v) : Middle<T>(v) {}
};

// Test via ConstructorCallNode (generateConstructorCallIr path: return Middle<T>{v})
template<typename T>
Middle<T> makeMiddle(T v) {
	return Middle<T>{v};
}

int main() {
	// Fix 1a: visitConstructorDeclarationNode base-init path
	Top<int> ti(7);
	if (ti.val != 7) return 1;

	Top<double> td(2.5);
	if (static_cast<int>(td.val * 2) != 5) return 2;

	// Fix 1b: generateConstructorCallIr aggregate path
	auto m1 = makeMiddle(42);
	if (m1.val != 42) return 3;

	auto m2 = makeMiddle(1.5f);
	if (static_cast<int>(m2.val * 2) != 3) return 4;

	return 0;
}
