// Test: explicit template instantiation with non-type template parameter.
// Before the Kind::Value skip was added to try_instantiate_template_explicit's
// TypeInfo registration loop, calling add_n<int, 3>(2) would register a TypeInfo
// entry for "N" with an uninitialised/garbage type in gTypesByName, corrupting
// the type table.  The test verifies the explicit-args path works correctly.
template<typename T, int N>
T add_n(T value) { return value + N; }

int main() {
	return add_n<int, 3>(2);  // explicit T=int, N=3; result = 2+3 = 5
}
