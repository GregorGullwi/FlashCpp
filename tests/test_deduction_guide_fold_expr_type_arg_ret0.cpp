// Regression: deduction-guide target with a fold-expression non-type template argument
// in a nested alias template argument (libstdc++ <array>-style pattern).
// Previously failed with:
//   "Expected ';' after deduction guide"

template<typename A, typename B>
inline constexpr bool same_v = false;

template<typename A>
inline constexpr bool same_v<A, A> = true;

template<bool B, typename T = void>
struct enable_if {};

template<typename T>
struct enable_if<true, T> { using type = T; };

template<bool B, typename T = void>
using enable_if_t = typename enable_if<B, T>::type;

template<typename T, int N>
struct array {
	T data[N];
};

template<typename _Tp, typename... _Up>
array(_Tp, _Up...) -> array<
	enable_if_t<(same_v<_Tp, _Up> && ...), _Tp>,
	1 + sizeof...(_Up)>;

int main() {
	return 0;
}
