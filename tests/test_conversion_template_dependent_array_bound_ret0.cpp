// A conversion-function template whose result is an array reference with a
// dependent bound must deduce that bound from the target reference. The alias
// target is only a canonical pattern until the bound is deduced, so rejecting
// the pattern instead of binding it loses an otherwise valid conversion.

template <class T, int N>
using ArrayRef = T (&)[N];

template <class T, int Bound>
struct ArraySource {
	T data[Bound];

	template <int N>
	operator ArrayRef<T, N>() const {
		return data;
	}
};

template <class T, int N>
int write_through(T (&values)[N], T value) {
	values[N - 1] = value;
	return static_cast<int>(sizeof(values) / sizeof(values[0]));
}

int main() {
	ArraySource<int, 3> small;
	int (&small_ref)[3] = small;
	if (write_through(small_ref, 19) != 3) {
		return 1;
	}
	if (small.data[2] != 19) {
		return 2;
	}

	ArraySource<long long, 5> wide;
	long long (&wide_ref)[5] = wide;
	if (write_through(wide_ref, 23) != 5) {
		return 3;
	}
	if (wide.data[4] != 23) {
		return 4;
	}

	ArraySource<short, 2> narrow;
	short (&narrow_ref)[2] = narrow;
	if (write_through(narrow_ref, 7) != 2) {
		return 5;
	}
	if (narrow.data[1] != 7) {
		return 6;
	}
	return 0;
}
