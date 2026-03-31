// Regression: variable template partial specializations must accept bool NTTP pattern arguments.

template <class From, class To, bool = true, bool = true>
constexpr bool is_selected = true;

template <class From, class To, bool IsVoid>
constexpr bool is_selected<From, To, false, IsVoid> = false;

int main() {
	return is_selected<int, int, false, true> ? 1 : 0;
}
