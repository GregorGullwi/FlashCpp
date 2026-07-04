namespace std {
	enum class byte : unsigned char {};

	template <class IntType>
	constexpr byte operator<<(byte value, IntType shift) noexcept {
		return static_cast<byte>(static_cast<unsigned int>(value) << shift);
	}

	template <class T>
	constexpr T&& move(T& value) noexcept {
		return static_cast<T&&>(value);
	}

	template <class T>
	constexpr void swap(T& left, T& right) noexcept(__is_nothrow_assignable(T&, T)) {
		T tmp = std::move(left);
		left = std::move(right);
		right = std::move(tmp);
	}

	template <class T>
	constexpr bool is_nothrow_move_constructible_v = __is_nothrow_constructible(T, T&&);

	template <class T>
	constexpr bool is_nothrow_move_assignable_v = __is_nothrow_assignable(T&, T);
}

int main() {
	std::byte shifted = static_cast<std::byte>(1) << 1;
	if (static_cast<unsigned int>(shifted) != 2) {
		return 1;
	}

	std::byte left = static_cast<std::byte>(1);
	std::byte right = static_cast<std::byte>(2);
	std::swap(left, right);
	if (static_cast<unsigned int>(left) != 2 ||
		static_cast<unsigned int>(right) != 1) {
		return 2;
	}

	if (!std::is_nothrow_move_constructible_v<std::byte>) {
		return 3;
	}
	if (!std::is_nothrow_move_assignable_v<std::byte>) {
		return 4;
	}
	return 0;
}
