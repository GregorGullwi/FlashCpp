// Regression: nested out-of-line constructor templates in class templates must
// attach replay metadata using canonical substituted signatures, even when the
// inner template parameter names differ between declaration and definition.

template <typename T>
struct Outer {
	struct Inner {
		int left;
		int right;

		template <typename U>
		Inner(U a, U b);

		int total() const {
			return left + right;
		}
	};
};

template <typename T>
template <typename V>
Outer<T>::Inner::Inner(V a, V b)
	: left(static_cast<int>(a)),
	  right(static_cast<int>(b)) {}

int main() {
	Outer<int>::Inner value(20, 22);
	return value.total();
}
