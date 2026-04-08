// Regression test: deferred alias-template member materialization during template parsing.

template <typename T, T V>
struct integral_constant {
	static constexpr T value = V;
};

template <bool B>
struct choose_value {
	using type = integral_constant<int, 2>;
};

template <>
struct choose_value<true> {
	using type = integral_constant<int, 40>;
};

template <bool B>
using choose_value_alias = choose_value<B>;

template <bool B>
struct holder {
	using selected = typename choose_value_alias<B>::type;
	static constexpr int value = selected::value;
};

int main() {
	return holder<true>::value + holder<false>::value;
}
