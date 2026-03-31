template <typename T, T V>
struct integral_constant {
	static constexpr T value = V;
};

using true_type = integral_constant<bool, true>;

template <typename T>
struct is_integer {
	static constexpr bool value = false;
};

template <>
struct is_integer<int> : true_type {};

int main() {
	return is_integer<int>::value ? 42 : 0;
}
