// Regression: qualified nested type lookup through an instantiated dependent base
// should find inherited aliases from the resolved base class.
template<typename T>
struct Base {
	using value_type = int;
};

template<typename T>
struct Derived : Base<T> {};

int main() {
	typename Derived<int>::value_type x = 0;
	return x;
}
