// Phase 2 validation: alias-template use through struct-local `using`
// Verifies that a using-declaration inside a struct body that names an
// alias-template instantiation correctly resolves to the underlying class
// template specialization.  Container::Inner must behave as Wrapper<int>.

template<typename T>
struct Wrapper {
	T data;
};

template<typename T>
using WrapAlias = Wrapper<T>;

struct Container {
	using Inner = WrapAlias<int>;
	Inner wrapped;
};

int main() {
	Container c;
	c.wrapped.data = 10;
	return c.wrapped.data;
}
