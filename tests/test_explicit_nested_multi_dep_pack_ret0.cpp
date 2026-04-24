// Regression test for explicit + deduced nested multi-dependent packs.
// The explicit path must still consume the wrapped pack call-arg slice.

template<typename T, typename U>
struct Pair {
	T first;
	U second;
};

template<typename T>
struct Wrap {
	T value;
};

template<int N, typename... Ts, typename... Us>
int sum_with_offset(Wrap<Pair<Ts, Us>>... xs) {
	return N + (0 + ... + (static_cast<int>(xs.value.first) + static_cast<int>(xs.value.second)));
}

int main() {
	Wrap<Pair<int, double>> a{{1, 2.0}};
	Wrap<Pair<char, int>> b{{3, 4}};
	Wrap<Pair<int, int>> c{{5, 6}};

	if (sum_with_offset<10>(a, b) != 20) return 1;
	if (sum_with_offset<5>(a, b, c) != 26) return 2;
	return 0;
}
