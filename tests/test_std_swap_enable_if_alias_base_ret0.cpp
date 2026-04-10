template<bool Cond, typename Type = void>
struct EnableIf { };

template<typename Type>
struct EnableIf<true, Type> {
	using type = Type;
};

template<bool Value>
struct BoolConstant {
	static constexpr bool value = Value;
};

template<typename Type>
struct IsMoveConstructible : BoolConstant<true> { };

template<typename Type>
struct IsMoveAssignable : BoolConstant<true> { };

template<typename Type>
struct IsTupleLike : BoolConstant<false> { };

template<typename Trait>
struct NotTrait : BoolConstant<!Trait::value> { };

template<typename...>
struct AndTraits;

template<>
struct AndTraits<> : BoolConstant<true> { };

template<typename First>
struct AndTraits<First> : First { };

template<typename First, typename... Rest>
struct AndTraits<First, Rest...> : EnableIf<First::value, AndTraits<Rest...>>::type { };

static_assert(AndTraits<NotTrait<IsTupleLike<int>>, IsMoveConstructible<int>, IsMoveAssignable<int>>::value);

int main() {
	return 0;
}
