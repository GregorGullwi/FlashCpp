// Test: Global-scope class template instantiated from inside a namespace.
// Regression test for ns_path.empty() conflation bug: buildNamespacePathFromHandle
// returns empty for global-scope structs, but compute_and_set_mangled_name's
// fallback treated that as "struct not found" and used the instantiation-site
// namespace (bar), producing a mangled name with "bar::" prefix.
// The call site (CodeGen_Call_Indirect) derives namespace purely from the struct's
// NamespaceHandle (global → no prefix), causing a definition/call mismatch → link error.
//
// This is distinct from test_lazy_nested_global_scope_ret42.cpp which uses nested
// types (Container<T>::Inner) whose qualified names contain "::", bypassing the
// affected code path.

template<typename T>
struct Foo {
	T value;
	T get() const { return value; }
};

namespace bar {
	int f() {
		Foo<int> x{42};
		return x.get();
	}
}

int main() {
	return bar::f(); // Expected: 42
}
