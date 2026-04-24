// Regression test for nested multi-dependent pack deduction.
// Wrap<Pair<Ts,Us>>... must deduce both Ts and Us through the outer wrapper.

template<typename T, typename U>
struct Pair {
	T first;
	U second;
};

template<typename T>
struct Wrap {
	T value;
};

template<typename... Ts, typename... Us>
int count_nested_pairs(Wrap<Pair<Ts, Us>>...) {
	return static_cast<int>(sizeof...(Ts)) + static_cast<int>(sizeof...(Us));
}

template<typename... Ts, typename... Us>
int sum_nested_pairs(Wrap<Pair<Ts, Us>>... xs) {
	return (0 + ... + (static_cast<int>(xs.value.first) + static_cast<int>(xs.value.second)));
}

int main() {
	Wrap<Pair<int, double>> a{{1, 2.0}};
	Wrap<Pair<char, int>> b{{3, 4}};

	if (count_nested_pairs(a, b) != 4) return 1;
	if (sum_nested_pairs(a, b) != 10) return 2;
	return 0;
}
