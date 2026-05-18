template<bool Condition, typename TrueType, typename FalseType>
struct Conditional {
	using type = TrueType;
};

template<typename TrueType, typename FalseType>
struct Conditional<false, TrueType, FalseType> {
	using type = FalseType;
};

template<bool Condition, typename TrueType, typename FalseType>
using ConditionalT = typename Conditional<Condition, TrueType, FalseType>::type;

struct FalseType {
	static constexpr bool value = false;
};

struct TrueType {
	static constexpr bool value = true;
};

template<typename Type>
struct IsEmpty : ConditionalT<__is_empty(Type), TrueType, FalseType> {
};

template<typename Type>
using EmptyAlias = IsEmpty<Type>;

template<typename Type>
using EmptyNotFinal = ConditionalT<__is_final(Type), FalseType, EmptyAlias<Type>>;

template<unsigned long Index, typename Head, bool = EmptyNotFinal<Head>::value>
struct HeadBase {
	static constexpr int value = 10;
};

template<unsigned long Index, typename Head>
struct HeadBase<Index, Head, true> {
	static constexpr int value = 0;
};

struct Empty {
};

struct NonEmpty {
	int member;
};

struct FinalHead final {
};

int main() {
	return HeadBase<0, Empty>::value +
		HeadBase<1, NonEmpty>::value -
		HeadBase<2, FinalHead>::value;
}
