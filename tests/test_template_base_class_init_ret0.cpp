// Test: template class constructor initializer list correctly routes base class
// initializers to add_base_initializer (not add_member_initializer).

template <typename T>
struct Base {
	T x;
	T y;
	Base(T a, T b) : x(a), y(b) {}
};

template <typename T>
struct Derived : Base<T> {
	T z;
	Derived(T a, T b, T c) : Base<T>(a, b), z(c) {}
};

int main() {
	Derived<int> d(10, 20, 30);
	if (d.x != 10)
		return 1;
	if (d.y != 20)
		return 2;
	if (d.z != 30)
		return 3;

	Derived<double> dd(1.5, 2.5, 3.5);
	if (dd.x != 1.5)
		return 4;
	if (dd.y != 2.5)
		return 5;
	if (dd.z != 3.5)
		return 6;

	return 0;
}
