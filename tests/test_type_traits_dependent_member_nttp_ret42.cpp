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
	bool const_volatile_int_is_integral = is_integral_custom<const volatile int>::value;
	bool char_is_integral = is_integral_custom<const volatile char>::value;
	return const_volatile_int_is_integral ? (char_is_integral ? 0 : 42) : 0;
}
