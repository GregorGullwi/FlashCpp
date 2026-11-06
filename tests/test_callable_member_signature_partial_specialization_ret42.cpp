// Member callable declarations represented with a legacy return-type/pointer
// spelling must compare as the same canonical function-pointer template
// argument during partial-specialization deduction.
template<class T, class U>
struct Same {
	static constexpr bool value = false;
};

template<class T>
struct Same<T, T> {
	static constexpr bool value = true;
};

template<class R, class... A>
struct Packed {
	R (*value)(A...);
};

struct ConcreteEmpty {
	long long (*value)();
};

struct ConcreteFull {
	long long (*value)(short, int);
};

using EmptyFunction = long long (*)();
using FullFunction = long long (*)(short, int);

static_assert(__is_same(decltype(Packed<long long>::value), EmptyFunction));
static_assert(Same<decltype(Packed<long long>::value), EmptyFunction>::value);
static_assert(Same<decltype(Packed<long long>::value), decltype(ConcreteEmpty::value)>::value);
static_assert(Same<decltype(Packed<long long, short, int>::value), FullFunction>::value);
static_assert(Same<decltype(Packed<long long, short, int>::value), decltype(ConcreteFull::value)>::value);
static_assert(!Same<decltype(Packed<long long>::value), FullFunction>::value);

int main() {
	return Same<decltype(Packed<long long>::value), decltype(ConcreteEmpty::value)>::value &&
				   Same<decltype(Packed<long long, short, int>::value), decltype(ConcreteFull::value)>::value
			   ? 42
			   : 1;
}
