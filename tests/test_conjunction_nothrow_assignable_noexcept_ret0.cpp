// MSVC pair/map assignment uses
//   noexcept(conjunction_v<is_nothrow_copy_assignable<T1>, ...>)
// where each trait's ::value comes from __is_nothrow_assignable and
// conjunction inherits that ::value through
//   _Conjunction<static_cast<bool>(First::value), First, Rest...>::type

template <bool Value>
struct BoolConstant {
	static constexpr bool value = Value;
};

using TrueType = BoolConstant<true>;

template <bool FirstValue, class First, class... Rest>
struct ConjunctionImpl {
	using type = First;
};

template <class True, class Next, class... Rest>
struct ConjunctionImpl<true, True, Next, Rest...> {
	using type = typename ConjunctionImpl<static_cast<bool>(Next::value), Next, Rest...>::type;
};

template <class... Traits>
struct Conjunction : TrueType {};

template <class First, class... Rest>
struct Conjunction<First, Rest...>
	: ConjunctionImpl<static_cast<bool>(First::value), First, Rest...>::type {};

template <class... Traits>
constexpr bool ConjunctionV = Conjunction<Traits...>::value;

template <class Type>
struct IsNothrowCopyAssignable {
	static constexpr bool value = __is_nothrow_assignable(Type&, const Type&);
};

template <class First, class Second>
struct Pair {
	First first;
	Second second;

	void assign(const Pair& other)
		noexcept(ConjunctionV<IsNothrowCopyAssignable<First>, IsNothrowCopyAssignable<Second>>) {
		first = other.first;
		second = other.second;
	}
};

struct Tiny {
	char byte;
};

struct Wide {
	int a;
	int b;
};

int main() {
	Pair<char, Tiny> tiny_pair{};
	Pair<int, Wide> wide_pair{};
	Pair<char, Tiny> tiny_other{};
	Pair<int, Wide> wide_other{};
	tiny_pair.assign(tiny_other);
	wide_pair.assign(wide_other);
	return (noexcept(tiny_pair.assign(tiny_other)) &&
			noexcept(wide_pair.assign(wide_other)))
		? 0
		: 1;
}
