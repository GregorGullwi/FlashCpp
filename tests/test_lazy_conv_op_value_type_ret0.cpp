// Verify: lazy materialized conversion operator uses canonical name (not original "operator value_type").
// Expected return: 0 (42 - 42)

template <typename T>
struct LazyWrapper {
	using value_type = T;
	T value_;
	LazyWrapper(T v) : value_(v) {}
	operator value_type() const { return value_; }
};

int main() {
	LazyWrapper<int> w(42);
	int x = w;
	return x - 42;  // 0 if correct
}
