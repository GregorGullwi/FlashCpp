// Regression test: explicit function-template arguments that mix a scalar NTTP
// with a trailing type pack must preserve the NTTP while expanding the pack.
template <int N, typename... Ts>
int target() {
	return 40 + static_cast<int>(sizeof...(Ts));
}

template <int N, typename... Ts>
int wrapper() {
	return target<N, Ts...>();
}

int main() {
	return wrapper<4, char, short>() == 42 ? 0 : 1;
}
