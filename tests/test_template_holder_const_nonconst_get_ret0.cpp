// Both const and non-const overloads of a template member function must
// have their bodies materialized, even when the lazy registry is queried
// through the CV-unaware *Any path.
// Expected return: 0
template <typename T>
struct Holder {
	using value_type = T;
	T value_;
	constexpr Holder(T v) : value_(v) {}
	constexpr value_type get() const { return value_; }
	constexpr value_type get() { return value_ + 1; }
};
int main() {
	// Runtime path: exercises IrGenerator_Call_Direct *Any lazy instantiation.
	Holder<int> h(10);
	int a = h.get();			 // non-const: 11
	if (a != 11)
		return 1;
	const Holder<int> ch(10);
	int b = ch.get();		  // const: 10
	if (b != 10)
		return 2;
	// Constexpr path: exercises ConstExprEvaluator *Any lazy instantiation.
	constexpr Holder<int> ce(20);
	constexpr int c = ce.get();	// const (constexpr objects are const): 20
	if (c != 20)
		return 3;
	return 0;
}
