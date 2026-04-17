// Regression test: namespaced out-of-line operator() on a class-template member
// function template must still instantiate from calls on the concrete type.

namespace math {
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
}

int main() {
	math::Adder<int> add(40);
	if (add(2) != 42)
		return 1;
	return 0;
}
