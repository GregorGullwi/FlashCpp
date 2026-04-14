// Regression: function template deduction must preserve inner non-type arguments
// when deducing through a template-template parameter.

template <typename T, int N>
struct Array {
	T data;
	static constexpr int size = N;
};

template <template <typename, int> class C, typename T, int N>
int useMixed(C<T, N>& c) {
	return c.data + C<T, N>::size;
}

int main() {
	Array<int, 3> a{4};
	return useMixed(a) - 7;
}
