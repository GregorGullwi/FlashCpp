template <typename T, T V>
struct integral_constant {
	static constexpr T value = V;
	using type = integral_constant;
};

template <int N>
struct static_sign : integral_constant<int, (N < 0) ? -1 : 1> {};

template <int N>
struct static_abs : integral_constant<int, N * static_sign<N>::value> {};

template <int Num, int Den = 1>
struct ratio {
	static constexpr int num = Num * static_sign<Den>::value;
	static constexpr int den = static_abs<Den>::value;
};

template <typename R>
struct ratio_sum_base : integral_constant<int, R::num + R::den> {};

template <typename R>
struct ratio_sum : ratio_sum_base<R>::type {};

template <typename R>
struct ratio_sum_chain : ratio_sum<R>::type {};

template <typename R, int = ratio_sum_chain<R>::value>
struct ratio_probe {
	static constexpr int value = 1;
};

template <typename R>
struct ratio_probe<R, 3> {
	static constexpr int value = 7;
};

int main() {
	using half = ratio<1, 2>;

	static_assert(ratio_sum<half>::value == 3);
	static_assert(ratio_sum_chain<half>::value == 3);

	return ratio_probe<half>::value == 7 ? 0 : 1;
}
