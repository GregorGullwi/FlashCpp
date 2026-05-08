template<bool B>
struct Conditional;

template<>
struct Conditional<true> {
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

struct TrueChoice {
	static constexpr bool value = true;
};

struct FalseChoice {
	static constexpr bool value = false;
};

struct NonEmpty {
	int value;
};

template<class T, bool Empty = ConditionalT<__is_empty(T), TrueChoice, FalseChoice>::value>
struct UsesAliasDefault {
	static constexpr bool value = Empty;
};

int main() {
	return UsesAliasDefault<NonEmpty>::value ? 1 : 0;
}
