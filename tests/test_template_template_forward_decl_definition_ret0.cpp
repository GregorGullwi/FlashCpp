// Regression: a function template with a forward declaration and later
// definition must instantiate from the definition, not the bodyless
// declaration. The body also ODR-uses a template-template qualified static
// member so the reparse path keeps the live TTP substitution.

template <typename T, int N>
struct Array {
	T data;
	static constexpr int size = N;
};

template <template <typename, int> class C, typename T, int N>
int useMixed(C<T, N>& c);

template <template <typename, int> class C, typename T, int N>
int useMixed(C<T, N>& c) {
	const int& size_ref = C<T, N>::size;
	return c.data + size_ref;
}

int main() {
	Array<int, 3> a{4};
	return useMixed(a) - 7;
}
