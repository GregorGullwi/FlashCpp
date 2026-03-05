// Test: Nested class constructor that references the outer template parameter T.
// Exercises the code path where nested class constructors are added via
// addConstructor() but NOT registered with LazyMemberInstantiationRegistry.
// If template parameter substitution is missing for nested constructors,
// the constructor body/parameters won't have T replaced with int,
// causing compile or link errors.

template<typename T>
struct Container {
	struct Inner {
		T value;
		Inner(T v) : value(v) {}
		T get() const { return value; }
	};
};

int main() {
	Container<int>::Inner i(42);
	return i.get(); // Expected: 42
}
