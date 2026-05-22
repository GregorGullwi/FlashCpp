// Regression: top-level out-of-line constructor templates on class templates
// must match signatures that mix outer class-template params and inner
// constructor-template params.

template <typename T>
struct PairBox {
	T first;
	int second;

	template <typename U>
	PairBox(T a, U b);

	int total() const {
		return static_cast<int>(first) + second;
	}
};

template <typename T>
template <typename U>
PairBox<T>::PairBox(T a, U b)
	: first(a),
	  second(static_cast<int>(b)) {}

int main() {
	PairBox<int> value(20, 22);
	return value.total();
}
