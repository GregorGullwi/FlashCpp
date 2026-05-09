template<class T, T V>
struct IntegralConstant {
static constexpr T value = V;
};

using FalseType = IntegralConstant<bool, false>;

template<class T>
struct IsEmpty : IntegralConstant<bool, __is_empty(T)> {
};

template<typename T>
struct IsEmptyNonTuple : IsEmpty<T> {
};

template<bool, typename If, typename Else>
struct Conditional {
using type = If;
};

template<typename If, typename Else>
struct Conditional<false, If, Else> {
using type = Else;
};

template<bool Cond, typename If, typename Else>
using ConditionalT = typename Conditional<Cond, If, Else>::type;

template<typename T>
using EmptyNotFinal = ConditionalT<__is_final(T), FalseType, IsEmptyNonTuple<T>>;

template<int Idx, typename Head, bool = EmptyNotFinal<Head>::value>
struct HeadBase;

template<int Idx, typename Head>
struct HeadBase<Idx, Head, true> {
};

template<int Idx, typename Head>
struct HeadBase<Idx, Head, false> {
};

template<int Idx, typename... Elements>
struct TupleImpl;

template<int Idx, typename Head, typename... Tail>
struct TupleImpl<Idx, Head, Tail...> : HeadBase<Idx, Head> {
};

struct NonEmpty {
int value;
};

int main() {
TupleImpl<0, NonEmpty>* ptr = nullptr;
return ptr != nullptr;
}
