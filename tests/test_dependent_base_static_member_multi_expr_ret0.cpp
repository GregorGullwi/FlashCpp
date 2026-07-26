namespace traits {
	template <typename T, T Value>
	struct integral_constant {
		static constexpr T value = Value;
	};

	template <bool Value>
	using bool_constant = integral_constant<bool, Value>;

	constexpr long long absolute(long long value) {
		return value < 0 ? -value : value;
	}

	constexpr long long sign(long long value) {
		return value < 0 ? -1 : 1;
	}

	constexpr long long gcd(long long left, long long right) {
		left = absolute(left);
		right = absolute(right);
		while (right != 0) {
			const long long previous_left = left;
			left = right;
			right = previous_left % right;
		}
		return left;
	}

	template <long long Num, long long Den>
	struct ratio {
		static constexpr long long num = sign(Num) * sign(Den) * absolute(Num) / gcd(Num, Den);
		static constexpr long long den = absolute(Den) / gcd(Num, Den);
	};

	template <typename Left, typename Right>
	struct ratio_equal
		: bool_constant<Left::num == Right::num && Left::den == Right::den> {};
}

int main() {
	using half = traits::ratio<1, 2>;
	using equivalent_half = traits::ratio<2, 4>;
	using third = traits::ratio<1, 3>;

	static_assert(traits::ratio_equal<half, equivalent_half>::value);
	static_assert(!traits::ratio_equal<half, third>::value);
	return 0;
}
