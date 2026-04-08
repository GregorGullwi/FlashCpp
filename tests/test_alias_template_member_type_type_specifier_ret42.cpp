// Regression test: direct ordinary type parsing for alias templates that materialize a member type.

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
using choose_value_t = typename choose_value<B>::type;

int main() {
	choose_value_t<true> yes;
	choose_value_t<false> no;
	return yes.value + no.value;
}
