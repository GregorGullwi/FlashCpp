// Test: Global-scope template with nested type, instantiated from inside a namespace.
// Verifies that instantiateLazyNestedType assigns the global namespace (not the
// instantiation-site namespace "foo") when the parent class is at global scope.

template<typename T>
struct Container {
	struct Inner {
		T value;
		T get() const { return value; }
	};
};

namespace foo {
	// Instantiating Container<int> from within namespace foo.
	// Container::Inner should be registered in the global namespace, not "foo".
	int make() {
		Container<int>::Inner i{42};
		return i.get();
	}
}

int main() {
	return foo::make(); // Expected: 42
}
