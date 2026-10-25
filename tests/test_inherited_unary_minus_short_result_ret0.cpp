template<class T> struct Base {
	T value;
	T operator-() const { return -value; }
};
template<class T> struct Derived : Base<T> {};
struct Padding { long long guard; };
struct Offset : Padding, Base<short> {};
struct IntResult { short value; int operator-() const { return -value; } };
struct IntDerived : IntResult {};
int main() {
	Base<short> direct;
	direct.value = 11;
	if (-direct != -11) return 1;
	Derived<short> inherited;
	inherited.value = 13;
	if (-inherited != -13) return 2;
	Offset offset;
	offset.guard = 9999;
	offset.value = 17;
	if (-offset != -17) return 3;
	IntDerived promoted;
	promoted.value = 19;
	if (-promoted != -19) return 4;
	inherited.value = -23;
	if (-inherited != 23) return 5;
	return 0;
}
