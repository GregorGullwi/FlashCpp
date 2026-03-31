// Test: typedef inherited from template base class used in non-template derived class
// This pattern is used by std::iterator and _Bit_iterator_base in stl_bvector.h
template <typename A, typename B>
struct Base {
	typedef A first_type;
	typedef B second_type;
};

struct Derived : public Base<long, int> {
	int val;

	int foo(long input) {
		first_type x = input;
		second_type y = 3;
		if (x < 0) {
			return static_cast<int>(-x) + y;
		}
		return static_cast<int>(x) + y;
	}
};

int main() {
	Derived d;
	d.val = 0;
	return d.foo(-3) - 6; // |-3| + 3 = 6, so 6 - 6 = 0
}
