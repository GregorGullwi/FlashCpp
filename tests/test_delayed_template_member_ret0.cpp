// Regression test: template class with delayed member function body parsing
// Verifies that template instantiation uses the unified delayed body parsing path.

template <typename T>
struct Holder {
	T value;

	Holder(T v) : value(v) {}

	T get() const { return this->value; }

	T add(T other) { return value + other; }
};

int main() {
	Holder<int> hi(10);
	if (hi.get() != 10)
		return 1;
	if (hi.add(5) != 15)
		return 2;

	Holder<int> hj(20);
	if (hj.get() != 20)
		return 3;
	if (hj.add(8) != 28)
		return 4;

	return 0;
}
