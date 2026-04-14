// Regression: function template deduction must preserve inner non-type arguments
// when deducing through a template-template parameter.

template <typename T, int N>
struct Array {
	T data;
	static constexpr int size = N;
};

template <template <typename, int> class C, typename T, int N>
int useMixed(C<T, N>& c);

template <typename T>
concept CanUseMixed = requires(T& value) {
	useMixed(value);
};

int main() {
	return CanUseMixed<Array<int, 3>> ? 0 : 1;
}
