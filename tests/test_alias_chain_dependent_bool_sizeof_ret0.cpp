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

template<>
struct is_integral<long long> {
	static constexpr bool value = true;
};

template<typename T>
using require_integral = enable_if_t<is_integral<T>::value, T>;

int main() {
	require_integral<int> x = 1;
	require_integral<long long> y = 2;
	return sizeof(x) == sizeof(int) && sizeof(y) == sizeof(long long) ? 0 : 1;
}
