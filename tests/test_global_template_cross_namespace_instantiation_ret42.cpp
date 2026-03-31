// Test: Global-scope template instantiated from MULTIPLE different namespaces,
// combined with a namespace-scoped template to verify no cross-contamination.
//
// Regression test for the ns_path.empty() conflation bug in compute_and_set_mangled_name:
// buildNamespacePathFromHandle returns empty for global-scope structs, but the
// fallback previously treated empty as "struct not found" and used the instantiation-site
// namespace, producing wrong mangled names (e.g., "alpha::Box$hash::get" instead of
// "Box$hash::get").
//
// This test is more adversarial than test_global_template_instantiated_from_namespace_ret42.cpp:
//   1. Two different namespaces instantiate the same global template
//   2. A namespace-scoped template is also instantiated to verify it keeps its namespace
//   3. Both direct and indirect member function calls are exercised

template <typename T>
struct Box {
	T val;
	T get() const { return val; }
};

namespace alpha {
template <typename T>
struct Wrapper {
	T inner;
	T unwrap() const { return inner; }
};

int from_alpha() {
	Box<int> b{10};			// global template, instantiated from alpha
	Wrapper<int> w{5};	   // alpha-scoped template
	return b.get() + w.unwrap(); // 10 + 5 = 15
}
} // namespace alpha

namespace beta {
int from_beta() {
	Box<int> b{20};			// same global template, instantiated from beta
	alpha::Wrapper<int> w{7}; // alpha-scoped template used from beta
	return b.get() + w.unwrap(); // 20 + 7 = 27
}
} // namespace beta

int main() {
	return alpha::from_alpha() + beta::from_beta(); // 15 + 27 = 42
}
