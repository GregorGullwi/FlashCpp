template <long long Num, long long Den = 1>
struct ratio {
	static constexpr long long num = Num;
	static constexpr long long den = Den;
};

template <typename R1, typename R2,
		  bool = (R1::num == 0 || R2::num == 0),
		  bool = false>
struct ratio_less_impl {
	static constexpr bool value = (R1::num * R2::den) < (R2::num * R1::den);
};

template <typename R1, typename R2>
struct ratio_less {
	static constexpr bool value = ratio_less_impl<R1, R2>::value;
};

template <typename R1, typename R2, bool = ratio_less<R1, R2>::value>
struct ratio_add_impl {
	static constexpr int value = 1;
};

template <typename R1, typename R2>
struct ratio_add_impl<R1, R2, true> {
	static constexpr int value = 7;
};

int main() {
	using half = ratio<1, 2>;
	using third = ratio<1, 3>;
	return ratio_add_impl<third, half>::value == 7 ? 0 : 1;
}
