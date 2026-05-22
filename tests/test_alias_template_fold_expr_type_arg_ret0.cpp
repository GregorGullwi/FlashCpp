// Regression: alias template target with a fold-expression non-type template argument.
// Previously failed with:
//   "Expected ';' after alias template declaration"
// because parse_explicit_template_arguments did not accept FoldExpressionNode
// as a dependent compile-time template argument.

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

template<typename T, typename... U>
using FoldAlias = enable_if_t<(same_v<T, U> && ...), T>;

int main() {
	FoldAlias<int, int, int> value = 7;
	(void)value;
	return 0;
}
