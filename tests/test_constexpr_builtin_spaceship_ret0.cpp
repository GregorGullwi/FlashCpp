namespace std {
	class strong_ordering {
		signed char _M_value;
	public:
		constexpr explicit strong_ordering(signed char v) noexcept
			: _M_value(v) {}

		static const strong_ordering less;
		static const strong_ordering equal;
		static const strong_ordering greater;

		friend constexpr bool operator>(strong_ordering v, int) noexcept { return v._M_value > 0; }
		friend constexpr bool operator<(strong_ordering v, int) noexcept { return v._M_value < 0; }
		friend constexpr bool operator==(strong_ordering v, int) noexcept { return v._M_value == 0; }
	};

	inline constexpr strong_ordering strong_ordering::less{static_cast<signed char>(-1)};
	inline constexpr strong_ordering strong_ordering::equal{static_cast<signed char>(0)};
	inline constexpr strong_ordering strong_ordering::greater{static_cast<signed char>(1)};
}

constexpr int result =
	((100u <=> 50u) > 0 &&
	 (0ull <=> 1ull) < 0 &&
	 (-5 <=> -5) == 0)
		? 0
		: 1;

static_assert(result == 0);

int main() {
	return result;
}
