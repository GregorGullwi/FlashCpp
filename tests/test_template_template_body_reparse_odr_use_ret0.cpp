// Regression: function-template body reparse must preserve template-template
// substitutions for ODR-used non-type arguments inside the body.

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
