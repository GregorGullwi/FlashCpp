template<bool B>
struct Select;

template<>
struct Select<true> {
	template<class T, class F>
	using type = T;
};

template<>
struct Select<false> {
	template<class T, class F>
	using type = F;
};

template<bool B, class T, class F>
using SelectT = typename Select<B>::template type<T, F>;

struct TrueChoice {
	static constexpr bool value = true;
};

struct FalseChoice {
	static constexpr bool value = false;
};

template<class T>
using SelectedValueOwner = SelectT<__is_empty(T), TrueChoice, FalseChoice>;

template<class T, bool Empty = SelectedValueOwner<T>::value>
struct UsesAliasDefault {
	static constexpr bool value = Empty;
};

struct NonEmpty {
	int value;
};

int main() {
	return UsesAliasDefault<NonEmpty>::value ? 1 : 0;
}
