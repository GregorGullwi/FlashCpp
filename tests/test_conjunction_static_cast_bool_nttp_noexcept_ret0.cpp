// MSVC <type_traits> implements conjunction as:
//   _Conjunction<static_cast<bool>(_First::value), _First, _Rest...>::type
// Instantiating that NTTP must fold Trait::value through static_cast<bool>
// so dependent noexcept(conjunction_v<...>) specifications can be constant.

template <bool Value>
struct BoolConstant {
	static constexpr bool value = Value;
};

using TrueType = BoolConstant<true>;
using FalseType = BoolConstant<false>;

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
struct IsNothrowMoveConstructible : BoolConstant<(sizeof(Type) > 1)> {};

template <class Type>
struct IsNothrowMoveAssignable : BoolConstant<(sizeof(Type) >= sizeof(int))> {};

template <class Type>
void swapValues(Type& left, Type& right)
	noexcept(ConjunctionV<IsNothrowMoveConstructible<Type>, IsNothrowMoveAssignable<Type>>) {
	Type tmp = left;
	left = right;
	right = tmp;
}

struct Tiny {
	char byte;
};

struct Wide {
	int a;
	int b;
};

int main() {
	Tiny tiny_a{'A'};
	Tiny tiny_b{'B'};
	Wide wide_a{1, 2};
	Wide wide_b{3, 4};
	swapValues(tiny_a, tiny_b);
	swapValues(wide_a, wide_b);
	const bool tiny_nothrow =
		noexcept(swapValues(tiny_a, tiny_b));
	const bool wide_nothrow =
		noexcept(swapValues(wide_a, wide_b));
	return (!tiny_nothrow && wide_nothrow && tiny_a.byte == 'B' && wide_a.a == 3) ? 0 : 1;
}
