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

struct selector {
	template <bool B>
	using pick = choose_value_t<B>;
};

int main() {
	return selector::pick<true>::value + selector::pick<false>::value;
}
