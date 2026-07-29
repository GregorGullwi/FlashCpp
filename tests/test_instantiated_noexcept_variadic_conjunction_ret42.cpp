template <bool Value>
struct BoolConstant {
	static constexpr bool value = Value;
};

template <bool, class First, class... Rest>
struct ConjunctionSelect {
	using type = First;
};

template <class True, class Next, class... Rest>
struct ConjunctionSelect<true, True, Next, Rest...> {
	using type = typename ConjunctionSelect<Next::value, Next, Rest...>::type;
};

template <class... Traits>
struct Conjunction : BoolConstant<true> {};

template <class First, class... Rest>
struct Conjunction<First, Rest...>
	: ConjunctionSelect<First::value, First, Rest...>::type {};

template <class... Traits>
constexpr bool ConjunctionValue = Conjunction<Traits...>::value;

template <class Type>
struct IsNothrowMoveConstructible : BoolConstant<(sizeof(Type) > 1)> {};

template <class To, class From>
struct IsNothrowAssignable : BoolConstant<(sizeof(To) >= sizeof(From))> {};

template <class Type, class Other = Type>
constexpr Type exchangeValue(Type& value, Other&& replacement)
	noexcept(ConjunctionValue<
		IsNothrowMoveConstructible<Type>,
		IsNothrowAssignable<Type&, Other>>) {
	Type old = value;
	value = static_cast<Other&&>(replacement);
	return old;
};

struct LargeValue {
	int value;

	constexpr operator int() const {
		return value;
	}
};

int main() {
	LargeValue value{20};
	return noexcept(exchangeValue(value, LargeValue{22})) ? 42 : 0;
}
