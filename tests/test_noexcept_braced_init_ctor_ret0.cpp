// Regression: braced-init constructor call inside noexcept(...) operand.

template <class T>
struct Wrap {
	T value;
	int length;

	constexpr Wrap(T val, int len) : value(val), length(len) {}

	constexpr bool unwrapped() const noexcept(
		noexcept(Wrap<T>{value, length})) {
		return true;
	}
};

int main() {
	Wrap<int> w{7, 3};
	return w.unwrapped() ? 0 : 1;
}
