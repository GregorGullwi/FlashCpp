// Validates lazy static-member instantiation with the standard <type_traits>
// shape: is_integral<T> inherits from __is_integral_helper<T>::type, where
// __is_integral_helper<int> specializes to true_type. The deferred-base
// resolution must propagate the alias-shaped argument through to the exact
// specialization so is_integral<int>::value is found via base-class lookup
// without any hardcoded trait fallback.
//
// NOTE: A more elaborate variant of this test exercises
// `integral_constant<bool, __is_integral_helper<typename remove_cv<T>::type>::value>`
// as a base. That pattern depends on resolving a chained dependent member-type
// access used as a non-type template argument; see docs/KNOWN_ISSUES.md.
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
struct __is_integral_helper : false_type {};

template <>
struct __is_integral_helper<int> : true_type {};

template <typename T>
struct is_integral : __is_integral_helper<T>::type {};

template <typename T>
int dispatch(T value) {
	if constexpr (is_integral<T>::value) {
		return static_cast<int>(value);
	}
	return 0;
}

int main() {
	int x = 21;
	int y = 21;
	return dispatch(x) + dispatch(y);
}
