template <class, class>
constexpr bool is_same_v = false;

template <class T>
constexpr bool is_same_v<T, T> = true;

template <class Ty>
constexpr bool is_integral_v = is_same_v<Ty, int>;

template <class Ty>
constexpr bool is_integral_fn() {
	return is_integral_v<Ty>;
}

template <bool Value>
struct bool_constant {
	static constexpr bool value = Value;
};

template <class Ty>
struct is_integral : bool_constant<is_integral_fn<Ty>()> {};

int main() {
	if (!is_integral<int>::value) return 1;
	if (is_integral<float>::value) return 2;
	return 0;
}
