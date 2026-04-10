template<bool Value>
struct BoolConstant {
	static constexpr bool value = Value;
};

template<bool Cond, typename Type = void>
struct EnableIf { };

template<typename Type>
struct EnableIf<true, Type> {
	using type = Type;
};

template<typename First, typename... Rest>
struct And : EnableIf<First::value, And<Rest...>>::type { };

template<typename Last>
struct And<Last> : Last { };

int main() {
	static_assert(And<BoolConstant<true>, BoolConstant<true>>::value);
	static_assert(!And<BoolConstant<true>, BoolConstant<false>>::value);
	return 0;
}
