// Regression: out-of-line class-template constructors must replay qualified
// base mem-initializers as base initializers during instantiation.

namespace N {
	template<class T>
	struct Base {
		int value;
		Base(int v) : value(v) {}
	};
}

template<class T>
struct Derived : N::Base<T> {
	Derived(int v);
};

template<class T>
Derived<T>::Derived(int v) : N::Base<T>(v) {}

int main() {
	Derived<int> d(42);
	return d.value == 42 ? 0 : 1;
}
