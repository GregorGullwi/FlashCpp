struct Base {
	int value;
	int operator-() const { return -value; }
	long long operator+(int right) const { return value + right; }
};
struct Derived : Base {};
struct Padding { long long guard; };
struct Offset : Padding, Base {};

template<class T> struct GenericBase {
	T value;
	T operator-() const { return -value; }
	T operator+(int right) const { return value + right; }
};
template<class T> struct Generic : GenericBase<T> {};
using Alias = Generic<long long>;

int main() {
	Derived plain;
	plain.value = 7;
	Alias specialized;
	specialized.value = 11;
	Offset offset;
	offset.guard = 9999;
	offset.value = 13;
	static_assert(sizeof(decltype(plain + 3)) == sizeof(long long));
	static_assert(sizeof(decltype(specialized + 5)) == sizeof(long long));
	if (-plain != -7) return 1;
	if (plain + 3 != 10) return 2;
	if (-specialized != -11) return 3;
	if (specialized + 5 != 16) return 4;
	if (offset + 4 != 17) return 5;
	return 0;
}
