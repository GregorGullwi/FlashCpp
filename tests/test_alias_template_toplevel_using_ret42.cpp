// Phase 2 validation: alias-template use through top-level `using`
// Ensures that a top-level non-template using declaration that names an
// alias-template instantiation correctly resolves to the underlying class
// template specialization.  IntBox must behave identically to Box<int>.

template<typename T>
struct Box {
	T val;
};

template<typename T>
using BoxAlias = Box<T>;

using IntBox = BoxAlias<int>;

int main() {
	IntBox b;
	b.val = 42;
	return b.val;
}
