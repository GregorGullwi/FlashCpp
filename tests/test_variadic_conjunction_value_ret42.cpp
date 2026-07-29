template <bool Value>
struct BoolConstantForValue {
	static constexpr bool value = Value;
};

template <bool, class First, class... Rest>
struct ConjunctionSelectForValue {
	using type = First;
};

template <class True, class Next, class... Rest>
struct ConjunctionSelectForValue<true, True, Next, Rest...> {
	using type =
		typename ConjunctionSelectForValue<Next::value, Next, Rest...>::type;
};

template <class... Traits>
struct ConjunctionForValue : BoolConstantForValue<true> {};

template <class First, class... Rest>
struct ConjunctionForValue<First, Rest...>
	: ConjunctionSelectForValue<First::value, First, Rest...>::type {};

template <class... Traits>
constexpr bool ConjunctionValueForValue =
	ConjunctionForValue<Traits...>::value;

struct FirstTrue : BoolConstantForValue<true> {};
struct SecondTrue : BoolConstantForValue<true> {};

int main() {
	return ConjunctionValueForValue<FirstTrue, SecondTrue> ? 42 : 0;
}
