struct Padding { long long guard; };
template<class T> struct Operators {
	T value;
	long long operator-() const { return -value - 3; }
	double operator+() const { return value + 0.5; }
	long long operator~() const { return value + 100; }
};
template<class T> struct Derived : Padding, Operators<T> {};
struct Virtual : Padding, virtual Operators<int> {};

namespace Custom {
	struct Value { short value; };
	long long operator-(const Value& object) { return object.value + 20; }
	double operator+(const Value& object) { return object.value + 0.25; }
	int operator~(const Value& object) { return object.value + 30; }
}

int main() {
	Derived<short> object;
	object.guard = 9999;
	object.value = 7;
	if (-object != -10) return 11;
	if (+object != 7.5) return 12;
	if (~object != 107) return 13;
	const Derived<short>& reference = object;
	if (-reference != -10 || +reference != 7.5 || ~reference != 107) return 2;
	Virtual virtual_object;
	virtual_object.value = 11;
	if (-virtual_object != -14 || +virtual_object != 11.5 || ~virtual_object != 111) return 3;
	const Virtual& virtual_reference = virtual_object;
	if (-virtual_reference != -14 || +virtual_reference != 11.5 || ~virtual_reference != 111) return 5;
	Custom::Value free_object;
	free_object.value = 5;
	if (-free_object != 25 || +free_object != 5.25 || ~free_object != 35) return 4;
	static_assert(sizeof(decltype(-free_object)) == sizeof(long long));
	static_assert(sizeof(decltype(+object)) == sizeof(double));
	return 0;
}
