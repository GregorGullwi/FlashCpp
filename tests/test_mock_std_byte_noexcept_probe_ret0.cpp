namespace std {
	enum class byte : unsigned char {};

	template <class T>
	constexpr bool is_nothrow_move_assignable_v = __is_nothrow_assignable(T&, T);

	template <class T>
	constexpr void assign_probe(T& left, T right) noexcept(is_nothrow_move_assignable_v<T>) {
		left = right;
	}
}

template <class T>
constexpr bool assign_probe_noexcept_v = noexcept(std::assign_probe(static_cast<T&>(*static_cast<T*>(0)), static_cast<T&&>(*static_cast<T*>(0))));

int main() {
	return assign_probe_noexcept_v<std::byte> ? 0 : 1;
}
