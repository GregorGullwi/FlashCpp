// Regression: function template deduction must preserve inner non-type arguments
// when deducing through a template-template parameter.
// The default on N (= 42) also tests that deduction overrides defaults — per
// C++ semantics, the deduced N=3 from Array<int,3> must win over the default.

template <typename T, int N>
struct Array {
	T data;
	static constexpr int size = N;
};

template <template <typename, int> class C, typename T, int N = 42>
int useMixed(C<T, N>& c);

template <typename T>
concept CanUseMixed = requires(T& value) {
	useMixed(value);
};

int main() {
	return CanUseMixed<Array<int, 3>> ? 0 : 1;
}
