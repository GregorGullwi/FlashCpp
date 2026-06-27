// Regression: hidden-friend comparison operators must remain viable when one
// operand reaches the overload via an implicit conversion through a constructor
// template such as std::_Literal_zero(T). This pattern appears in MSVC
// <compare>, where reverse-order operators forward to sibling hidden friends.
namespace std {
	enum class _Compare_ord : signed char {
		less = -1,
		equivalent = 0,
		greater = 1
	};

	using _Compare_t = signed char;

	struct _Literal_zero {
		template<class T>
		constexpr _Literal_zero(T) noexcept {}
	};

	class partial_ordering {
		_Compare_t _Value;

	public:
		static const partial_ordering less;
		static const partial_ordering equivalent;
		static const partial_ordering greater;

		constexpr partial_ordering(_Compare_t value) noexcept : _Value(value) {}

		friend constexpr bool operator==(const partial_ordering value, _Literal_zero) noexcept {
			return value._Value == 0;
		}

		friend constexpr bool operator>(const partial_ordering value, _Literal_zero) noexcept {
			return value._Value > 0;
		}

		friend constexpr bool operator<(const partial_ordering value, _Literal_zero) noexcept {
			return value._Value < 0;
		}

		friend constexpr bool operator<(_Literal_zero, const partial_ordering value) noexcept {
			return value > 0;
		}

		friend constexpr bool operator>(_Literal_zero, const partial_ordering value) noexcept {
			return value < 0;
		}

		friend constexpr bool operator<=(_Literal_zero, const partial_ordering value) noexcept {
			return !(value > 0);
		}

		friend constexpr bool operator>=(_Literal_zero, const partial_ordering value) noexcept {
			return !(value < 0);
		}
	};

	inline constexpr partial_ordering partial_ordering::less{static_cast<_Compare_t>(_Compare_ord::less)};
	inline constexpr partial_ordering partial_ordering::equivalent{static_cast<_Compare_t>(_Compare_ord::equivalent)};
	inline constexpr partial_ordering partial_ordering::greater{static_cast<_Compare_t>(_Compare_ord::greater)};
}

int main() {
	if (!(0 < std::partial_ordering::greater)) {
		return 1;
	}
	if (!(0 > std::partial_ordering::less)) {
		return 2;
	}
	if (!(0 <= std::partial_ordering::equivalent)) {
		return 3;
	}
	if (!(0 >= std::partial_ordering::equivalent)) {
		return 4;
	}
	if (!(std::partial_ordering::equivalent == 0)) {
		return 5;
	}
	return 0;
}
