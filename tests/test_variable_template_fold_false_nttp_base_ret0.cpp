// Regression: a variable template fold that evaluates to false must still
// materialize the dependent base class for bool_constant<false>.

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

static_assert(!traits::is_integral<float>::value);

int main() {
	return !traits::is_integral<float>::value ? 0 : 1;
}
