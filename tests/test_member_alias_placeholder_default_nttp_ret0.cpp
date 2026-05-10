template<class T, T V>
struct IntegralConstant {
	static constexpr T value = V;
};

using FalseType = IntegralConstant<bool, false>;

template<bool B>
struct Conditional {
	template<class T, class F>
	using type = T;
};

template<>
struct Conditional<false> {
	template<class T, class F>
	using type = F;
};

template<class T>
struct IsEmpty : IntegralConstant<bool, __is_empty(T)> {
};

template<class T>
struct IsEmptyNonTuple : IsEmpty<T> {
};

template<class T>
using EmptyNotFinal = typename Conditional<__is_final(T)>::template type<FalseType, IsEmptyNonTuple<T>>;

template<class T>
struct ForceCandidate {
	using type = IsEmptyNonTuple<T>;
	static constexpr bool value = EmptyNotFinal<T>::value;
};

template<int I, class H, bool = ForceCandidate<H>::value>
struct HeadBase;

template<int I, class H>
struct HeadBase<I, H, false> {
};

template<int I, class H>
struct HeadBase<I, H, true> {
};

template<int I, class... Elements>
struct TupleImpl;

template<int I, class H, class... Tail>
struct TupleImpl<I, H, Tail...> : HeadBase<I, H> {
};

struct NonEmpty {
	int value;
};

struct Empty {
};

int main() {
	constexpr unsigned long long non_empty_size = sizeof(TupleImpl<0, NonEmpty>);
	constexpr unsigned long long empty_size = sizeof(TupleImpl<1, Empty>);
	return (non_empty_size > 0 && empty_size > 0) ? 0 : 1;
}
