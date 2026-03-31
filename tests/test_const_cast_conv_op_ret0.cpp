// Test: const_cast forces the correct const/non-const conversion operator overload.
// const_cast<const T&>(non_const_obj) should dispatch to const operator.
// const_cast<T&>(const_obj) should dispatch to non-const operator.
template <typename T>
struct LazyWrapper {
	using value_type = T;
	T value_;
	LazyWrapper(T v) : value_(v) {}
	operator value_type() const { return value_; }		   // const:     returns value_ (42)
	operator value_type() { return value_ + 1; }	 // non-const: returns value_+1 (43)
};

int main() {
	LazyWrapper<int> w(42);

// non-const: calls non-const operator => 43
	int x = w;
	if (x != 43)
		return 1;

	const LazyWrapper<int> w2(42);
// const: calls const operator => 42
	int x2 = w2;
	if (x2 != 42)
		return 2;

// const_cast to const ref: forces const operator => 42
	int x3 = const_cast<const LazyWrapper<int>&>(w);
	if (x3 != 42)
		return 3;

// const_cast to non-const ref: forces non-const operator => 43
	int x4 = const_cast<LazyWrapper<int>&>(w2);
	if (x4 != 43)
		return 4;

	return 0;
}
