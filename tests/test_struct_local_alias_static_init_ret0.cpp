template<typename T, T v>
struct integral_constant {
	static constexpr T value = v;
};

template<bool B>
using bool_constant = integral_constant<bool, B>;

template<bool B>
struct wrapper_alias_init {
	using constant_type = bool_constant<B>;
	static constexpr bool value = constant_type::value;
};

int main() {
	int result = 0;
	if (!wrapper_alias_init<false>::value) {
		result += 1;
	}
	if (wrapper_alias_init<true>::value) {
		result += 1;
	}
	return result == 2 ? 0 : 1;
}
