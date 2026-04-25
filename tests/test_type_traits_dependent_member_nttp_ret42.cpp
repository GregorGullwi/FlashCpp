template <typename T, T V>
struct integral_constant {
	static constexpr T value = V;
	using value_type = T;
	using type = integral_constant;
};

using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

template <typename T>
struct remove_const {
	using type = T;
};

template <typename T>
struct remove_const<const T> {
	using type = T;
};

template <typename T>
struct remove_volatile {
	using type = T;
};

template <typename T>
struct remove_volatile<volatile T> {
	using type = T;
};

template <typename T>
struct remove_cv {
	using type = typename remove_const<typename remove_volatile<T>::type>::type;
};

template <typename T>
struct is_integral_helper : false_type {};

template <>
struct is_integral_helper<int> : true_type {};

template <typename T>
struct is_integral_custom
	: integral_constant<bool, is_integral_helper<typename remove_cv<T>::type>::value> {};

int main() {
	// All cv-qualifications of int should be recognised as integral after cv stripping
	bool plain_int = is_integral_custom<int>::value;
	bool const_int = is_integral_custom<const int>::value;
	bool volatile_int = is_integral_custom<volatile int>::value;
	bool const_volatile_int = is_integral_custom<const volatile int>::value;

	// No cv-qualification of char should be recognised as integral
	bool plain_char = is_integral_custom<char>::value;
	bool const_char = is_integral_custom<const char>::value;
	bool volatile_char = is_integral_custom<volatile char>::value;
	bool const_volatile_char = is_integral_custom<const volatile char>::value;

	bool all_int_cases = plain_int && const_int && volatile_int && const_volatile_int;
	bool no_char_cases = !plain_char && !const_char && !volatile_char && !const_volatile_char;
	return (all_int_cases && no_char_cases) ? 42 : 0;
}
