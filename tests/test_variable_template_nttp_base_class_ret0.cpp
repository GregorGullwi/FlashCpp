// Regression test for variable-template values used as base-class non-type arguments.
// Covers the chain: partial-spec variable template -> variable template alias -> NTTP
// base class. Mixes native scalar types and a user struct for coverage.
//
// Previously, inside a deferred base like `bool_constant<is_integral_v<Ty>>`, the
// inner variable template (`is_integral_v`) was evaluated with un-substituted
// template arguments, so `is_same_v<Ty, int>` would reach partial-spec lookup as
// `is_same_v<Ty, int>` (one arg still bound to the template parameter name,
// category=UserDefined), failing the consistency check in the partial spec
// `is_same_v<T, T>` and returning `false` for `is_integral_v<int>`.

template <class, class>
constexpr bool is_same_v = false;

template <class T>
constexpr bool is_same_v<T, T> = true;

template <class Ty>
constexpr bool is_integral_v = is_same_v<Ty, int>;

template <class Ty>
constexpr bool is_short_v = is_same_v<Ty, short>;

template <class Ty>
constexpr bool is_float_v = is_same_v<Ty, float>;

template <bool Value>
struct bool_constant {
	static constexpr bool value = Value;
};

template <class Ty>
struct is_integral : bool_constant<is_integral_v<Ty>> {};

template <class Ty>
struct is_short : bool_constant<is_short_v<Ty>> {};

template <class Ty>
struct is_float : bool_constant<is_float_v<Ty>> {};

struct MyStruct {};

int main() {
	// Positive cases: partial specialization must fire.
	if (!is_integral<int>::value) return 1;
	if (!is_short<short>::value) return 2;
	if (!is_float<float>::value) return 3;

	// Negative cases: primary (false) template must be chosen.
	if (is_integral<short>::value) return 4;
	if (is_integral<float>::value) return 5;
	if (is_integral<MyStruct>::value) return 6;
	if (is_short<int>::value) return 7;
	if (is_float<double>::value) return 8;
	if (is_float<MyStruct>::value) return 9;

	return 0;
}
