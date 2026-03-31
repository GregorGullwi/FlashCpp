// Verify: const and non-const conversion operator overloads are dispatched correctly.
template <typename T>
struct LazyWrapper {
	using value_type = T;
	T value_;
	LazyWrapper(T v) : value_(v) {}
	operator value_type() const { return value_; }
	operator value_type() { return value_ + 1; }
};

int main() {
	LazyWrapper<int> w(42);

	const int x = w;	 // non-const: returns 43
	if (x != 43)
		return 1;

	const LazyWrapper<int> w2(42);
	int x2 = w2;		 // const: returns 42
	if (x2 != 42)
		return 2;

	return 0;
}
