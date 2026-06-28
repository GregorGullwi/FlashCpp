// Regression: a variable template whose initializer is a fold over another
// variable template can feed a true value into a dependent base NTTP.

namespace traits {
template <class, class>
constexpr bool is_same_v = false;

template <class T>
constexpr bool is_same_v<T, T> = true;

template <class Ty, class... Types>
constexpr bool is_any_of_v = (is_same_v<Ty, Types> || ...);

template <class Ty>
constexpr bool is_integral_v = is_any_of_v<Ty, bool, char, short, unsigned short>;

template <bool Value>
struct bool_constant {
	static constexpr bool value = Value;
};

template <class Ty>
struct is_integral : bool_constant<is_integral_v<Ty>> {};
}

static_assert(traits::is_integral<char>::value);

int main() {
	return traits::is_integral<char>::value ? 0 : 1;
}
