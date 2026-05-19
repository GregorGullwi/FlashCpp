template <bool>
struct Select;

template <>
struct Select<true> {
	template <template <class> class Function, class Type>
	using Apply = typename Function<Type>::type;
};

template <class Type>
struct Wrap {
	using type = Type;
};

namespace std {
	template <class, class>
	inline constexpr bool is_same_v = false;

	template <class Type>
	inline constexpr bool is_same_v<Type, Type> = true;
}

using Result = typename Select<true>::template Apply<Wrap, int>;
static_assert(std::is_same_v<Result, int>);

int main() {
	return 0;
}
