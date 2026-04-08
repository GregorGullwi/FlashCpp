template<int Num, int Den>
struct ratio {
	static constexpr int num = Num;
	static constexpr int den = Den;
};

template<typename T>
constexpr bool is_ratio_v = false;

template<int Num, int Den>
constexpr bool is_ratio_v<ratio<Num, Den>> = true;

template<typename R1, typename R2>
constexpr bool are_both_ratios() {
	if constexpr (is_ratio_v<R1>) {
		if constexpr (is_ratio_v<R2>) {
			return true;
		}
	}
	return false;
}

template<typename R1, typename R2,
		 bool = (R1::num == 0 || R2::num == 0),
		 bool = false>
struct ratio_less_impl {
	static constexpr bool value = (R1::num * R2::den) < (R2::num * R1::den);
};

template<typename R1, typename R2>
struct ratio_less {
	static_assert(are_both_ratios<R1, R2>(), "both template arguments must be a ratio");
	static constexpr bool value = ratio_less_impl<R1, R2>::value;
};

int main() {
	using half = ratio<1, 2>;
	using third = ratio<1, 3>;
	return ratio_less<third, half>::value ? 0 : 1;
}
