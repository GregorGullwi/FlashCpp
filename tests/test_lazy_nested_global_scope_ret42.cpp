// Test: Global-scope template with nested type, instantiated from inside a namespace.
// Exercises the bug where instantiateLazyNestedType assigns the instantiation-site
// namespace (e.g., "foo") instead of global when the parent class is at global scope.
//
// Bug: the condition `parent_ns.isValid() && !parent_ns.isGlobal()` in
// instantiateLazyNestedType skips global-scope parents, leaving decl_ns as
// the instantiation-site namespace.

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
