// Expected failure: distilled MSVC <type_traits> std::is_integral pattern.
// A variable-template partial specialization feeds another variable template,
// then that value is used as a non-type template argument in a base class.
// FlashCpp currently evaluates is_integral<int>::value as false.

template <class, class>
constexpr bool is_same_v = false;

template <class T>
constexpr bool is_same_v<T, T> = true;

template <class Ty>
constexpr bool is_integral_v = is_same_v<Ty, int>;

template <bool Value>
struct bool_constant {
	static constexpr bool value = Value;
};

template <class Ty>
struct is_integral : bool_constant<is_integral_v<Ty>> {};

static_assert(is_integral<int>::value);

int main() {
	return is_integral<int>::value ? 0 : 1;
}
