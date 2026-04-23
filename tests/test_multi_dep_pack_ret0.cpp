// Test: multi-dependent pack element type deduction.
// A function-parameter pack whose element type contains multiple dependent
// template-parameter positions, e.g. Pair<Ts, Us>...
// Both Ts and Us should be deduced from the corresponding positions inside each
// call argument.

template<typename T, typename U>
struct Pair {
	T first;
	U second;
	Pair(T f, U s) : first(f), second(s) {}
};

// sizeof...(Ts) + sizeof...(Us) should be 2 + 2 = 4
template<typename... Ts, typename... Us>
int count_packs(Pair<Ts, Us>...) {
	return static_cast<int>(sizeof...(Ts)) + static_cast<int>(sizeof...(Us));
}

// Sum of all first and second fields:  (1+2) + (3+4) = 10
template<typename... Ts, typename... Us>
int sum_packs(Pair<Ts, Us>... pairs) {
	return (0 + ... + (static_cast<int>(pairs.first) + static_cast<int>(pairs.second)));
}

int main() {
	Pair<int, double> p1(1, 2.0);
	Pair<char, int>   p2(3, 4);

	// Test 1: sizeof both packs = 2 each
	int r1 = count_packs(p1, p2);
	if (r1 != 4) return 1;

	// Test 2: sum of all fields
	int r2 = sum_packs(p1, p2);
	if (r2 != 10) return 2;

	return 0;
}
