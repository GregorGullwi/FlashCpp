// Regression: replayed out-of-line template constructors must recognize
// template base initializers like Base<T>(...) as base initialization.

template <typename T>
struct Base {
	T first;
	T second;

	Base(T a, T b) : first(a), second(b) {}
};

template <typename T>
struct Derived : Base<T> {
	T third;

	Derived(T a, T b, T c);
};

template <typename T>
Derived<T>::Derived(T a, T b, T c) : Base<T>(a, b), third(c) {}

int main() {
	Derived<int> value(10, 20, 30);
	return (value.first + value.second + value.third) - 60;
}
