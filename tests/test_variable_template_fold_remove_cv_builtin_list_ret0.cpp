// Regression: a full type-traits-style boolean fold over a type pack must
// reduce after remove_cv_t<T> substitution and builtin type canonicalization.

namespace traits {
template <class T>
struct remove_cv {
	using type = T;
};

template <class T>
using remove_cv_t = typename remove_cv<T>::type;

template <class, class>
constexpr bool is_same_v = false;

template <class T>
constexpr bool is_same_v<T, T> = true;

template <class Ty, class... Types>
constexpr bool is_any_of_v = (is_same_v<Ty, Types> || ...);

template <class Ty>
constexpr bool is_integral_v =
	is_any_of_v<remove_cv_t<Ty>, bool, char, signed char, unsigned char,
		wchar_t, char8_t, char16_t, char32_t, short, unsigned short,
		int, unsigned int, long, unsigned long, long long, unsigned long long>;

template <bool Value>
struct bool_constant {
	static constexpr bool value = Value;
};

template <class Ty>
struct is_integral : bool_constant<is_integral_v<Ty>> {};
}

static_assert(traits::is_integral<int>::value);
static_assert(!traits::is_integral<float>::value);

int main() {
	return traits::is_integral<int>::value && !traits::is_integral<float>::value ? 0 : 1;
}
