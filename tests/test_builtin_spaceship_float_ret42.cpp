// Test: built-in floating-point three-way comparison (<=>) returns
// std::partial_ordering for ordered values.
namespace std {
	class partial_ordering {
		signed char _M_value;
	public:
		constexpr partial_ordering(signed char v) noexcept : _M_value(v) {}

		static const partial_ordering less;
		static const partial_ordering equivalent;
		static const partial_ordering greater;
		static const partial_ordering unordered;

		friend constexpr bool operator>(partial_ordering __v, int) noexcept { return __v._M_value > 0; }
		friend constexpr bool operator<(partial_ordering __v, int) noexcept { return __v._M_value < 0; }
		friend constexpr bool operator==(partial_ordering __v, int) noexcept { return __v._M_value == 0; }
	};

	inline constexpr partial_ordering partial_ordering::less{static_cast<signed char>(-1)};
	inline constexpr partial_ordering partial_ordering::equivalent{static_cast<signed char>(0)};
	inline constexpr partial_ordering partial_ordering::greater{static_cast<signed char>(1)};
	inline constexpr partial_ordering partial_ordering::unordered{static_cast<signed char>(-128)};
}

int main() {
	int ret = 0;

	double greater_lhs = 3.5;
	double greater_rhs = 1.25;
	std::partial_ordering greater_result = greater_lhs <=> greater_rhs;
	if (greater_result > 0) {
		ret += 10;
	}

	double equal_lhs = 2.0;
	double equal_rhs = 2.0;
	std::partial_ordering equal_result = equal_lhs <=> equal_rhs;
	if (equal_result == 0) {
		ret += 20;
	}

	double less_lhs = -1.5;
	double less_rhs = 2.0;
	std::partial_ordering less_result = less_lhs <=> less_rhs;
	if (less_result < 0) {
		ret += 12;
	}

	return ret;
}
