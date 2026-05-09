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

template<bool B, class T, class F>
using ConditionalT = typename Conditional<B>::template type<T, F>;

struct FalseType {
	static constexpr bool value = false;
};

struct TrueType {
	static constexpr bool value = true;
};

template<class T>
struct IsEmpty : ConditionalT<__is_empty(T), TrueType, FalseType> {
};

template<class T>
using EmptyNotFinal = ConditionalT<__is_final(T), FalseType, IsEmpty<T>>;

template<class T, bool B = EmptyNotFinal<T>::value>
struct UsesDefault {
	static constexpr bool value = B;
};

struct NonEmpty {
	int x;
};

int main() {
	return UsesDefault<NonEmpty>::value ? 1 : 0;
}
