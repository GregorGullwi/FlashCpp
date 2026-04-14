// Phase 2 validation: alias chain with dependent bool non-type argument
// Exercises alias-template materialization through a chain where a bool
// non-type argument is computed from a dependent trait (is_integral<T>::value).
// The alias chain is: require_integral -> enable_if_t -> enable_if<true,T>::type
// require_integral<int> must resolve to int.

template<bool B, typename T = void>
struct enable_if {};

template<typename T>
struct enable_if<true, T> {
	using type = T;
};

template<bool B, typename T = void>
using enable_if_t = typename enable_if<B, T>::type;

template<typename T>
struct is_integral {
	static constexpr bool value = false;
};

template<>
struct is_integral<int> {
	static constexpr bool value = true;
};

template<typename T>
using require_integral = enable_if_t<is_integral<T>::value, T>;

int main() {
	require_integral<int> x = 1;
	return x;
}
