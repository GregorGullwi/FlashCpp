// Regression test: out-of-line operator() on a class-template member function
// template must register under the owning class name rather than an empty key.

template<typename T>
struct Adder {
	T base;

	Adder(T value) : base(value) {}

	template<typename U>
	T operator()(U value) const;
};

template<typename T>
template<typename U>
T Adder<T>::operator()(U value) const {
	return base + static_cast<T>(value);
}

int main() {
	Adder<int> add(40);
	return add(2);
}
