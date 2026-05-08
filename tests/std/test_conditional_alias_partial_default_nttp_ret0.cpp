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
struct IsEmptyNonTuple : TrueType {
};

template<typename Type>
using EmptyNotFinal = ConditionalT<__is_final(Type), FalseType, IsEmptyNonTuple<Type>>;

template<unsigned long Index, typename Head, bool = EmptyNotFinal<Head>::value>
struct HeadBase;

template<unsigned long Index, typename Head>
struct HeadBase<Index, Head, true> {
	static constexpr int value = 0;
};

int main() {
	return HeadBase<0, int>::value;
}
