// Operators inherited from specialized class templates must report the declared
// result type of the selected overload. Each case differs from the operand type
// in size or value so a flat base projection or a copied lhs type changes the
// static_assert outcome or the runtime result.
template<class T> struct Wide {
	T value;
	long long operator+(int right) const { return static_cast<long long>(value) + right; }
	short operator-() const { return static_cast<short>(-value); }
};
template<class T> struct WideBase : Wide<T> {};
using WideInt = WideBase<int>;

template<class T> struct Holder {
	T value;
};
template<class T> struct HolderBase : Holder<T> {};
template<class T> long long operator*(const HolderBase<T>& left, int right) {
	return static_cast<long long>(left.value) * right;
}
using HolderLong = HolderBase<long long>;

struct Plain {
	int value;
	int operator+(int right) const { return value + right; }
};
struct PlainDerived : Plain {};

static_assert(sizeof(decltype(WideInt{} + 1)) == sizeof(long long), "inherited member result is not the lhs type");
static_assert(sizeof(decltype(-WideInt{})) == sizeof(short), "inherited unary result is not the declared type");
static_assert(sizeof(decltype(HolderLong{} * 2)) == sizeof(long long), "free operator result on a specialized base");
static_assert(sizeof(decltype(PlainDerived{} + 3)) == sizeof(int), "plain inherited member result");

int main() {
	WideInt wide;
	wide.value = 11;
	if (wide + 5 != 16) return 1;
	if (-wide != -11) return 2;

	HolderLong holder;
	holder.value = 4;
	if (holder * 3 != 12) return 3;

	PlainDerived plain;
	plain.value = 7;
	if (plain + 3 != 10) return 4;
	return 0;
}
