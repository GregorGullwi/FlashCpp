// Regression: member-template partial-specialization static member replay must
// keep enclosing outer template bindings available during substitution.
template <typename Outer, int N>
struct OuterBox {
	template <bool Active, typename...>
	struct Inner {
		static constexpr int value = 0;
	};

	template <typename T, typename... Rest>
	struct Inner<true, T, Rest...> {
		static constexpr int value = sizeof(Outer) + N + sizeof(T);
	};
};

int main() {
	OuterBox<long long, 8>::Inner<true, int> inner;
	int ok[(decltype(inner)::value == 20) ? 1 : -1];
	return sizeof(ok) == sizeof(int) ? 0 : 1;
}
