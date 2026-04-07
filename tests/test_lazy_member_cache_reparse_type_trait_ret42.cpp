template <typename T, T V>
struct integral_constant {
	static constexpr T value = V;
	using value_type = T;
	using type = integral_constant;

	constexpr operator value_type() const noexcept { return value; }
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
struct __is_integral_helper : false_type {};

template <>
struct __is_integral_helper<int> : true_type {};

template <typename T>
struct is_integral
	: integral_constant<bool, __is_integral_helper<typename remove_cv<T>::type>::value> {};

template <typename T>
int dispatch(T value) {
	if constexpr (is_integral<T>::value) {
		return static_cast<int>(value);
	}
	return 0;
}

int main() {
	volatile int x = 21;
	const volatile int y = 21;
	return dispatch(x) + dispatch(y);
}
