// Test that template template parameter defaults parse correctly without error
// The parser previously caused a parse error on the '=' token
template <typename T>
struct MyContainer {
	T value;
};

// Template with template template parameter with default - should parse without error
template <typename T, template <typename> class Container = MyContainer>
struct Wrapper {
	T data;
	Container<T> stored;
};

// Explicit usage (not relying on default) to verify basic functionality
template <typename T, template <typename> class Container>
struct Holder {
	T data;
};

// Namespace-qualified template template default.
// Verifies parse_type_specifier resolves qualified names (ns::Template) correctly.
namespace my_ns {
template <typename T>
struct NsContainer {
	T val;
};
} // namespace my_ns

// Parser should handle namespace-qualified default via its qualified name resolution
template <typename T, template <typename> class C = my_ns::NsContainer>
struct QualifiedWrapper {
	T data;
	C<T> stored;
};

int main() {
	Wrapper<int> w;
	w.data = 42;
	w.stored.value = w.data;

	QualifiedWrapper<int> qw;
	qw.data = 42;
	qw.stored.val = qw.data;

	return w.stored.value + qw.stored.val - 42;
}
