// Regression: replayed out-of-line template constructors must preserve
// multi-argument paren member initializers instead of dropping later args.

struct Pair {
	int left;
	int right;

	Pair(int a, int b) : left(a), right(b) {}
};

template <typename T>
struct Holder {
	Pair pair;

	Holder(T a, T b);
};

template <typename T>
Holder<T>::Holder(T a, T b) : pair(a, b) {}

int main() {
	Holder<int> value(20, 22);
	return value.pair.left + value.pair.right;
}
