// Test: explicit instantiation of a function-parameter pack whose element type
// has multiple dependent positions (Pair<Ts,Us>...).
// This exercises the fix where function_pack_template_param_name must be set
// to the first template param name ("Ts"), not the template name ("Pair").
//
// NOTE: tests are in a helper function rather than directly in main() to work
// around KI-001 (variadic-template pack call result corrupted when it is the
// very first call in a function scope).

template<typename T, typename U>
struct Pair {
	T first;
	U second;
	Pair(T f, U s) : first(f), second(s) {}
};

// Explicit path: N is given explicitly, Ts/Us deduced from the pair args
template<int N, typename... Ts, typename... Us>
int sum_with_offset(Pair<Ts, Us>... pairs) {
	return N + (0 + ... + (static_cast<int>(pairs.first) + static_cast<int>(pairs.second)));
}

// Empty pack: explicit call with zero pair args
template<typename... Ts, typename... Us>
int count_pairs(Pair<Ts, Us>...) {
	return static_cast<int>(sizeof...(Ts)) + static_cast<int>(sizeof...(Us));
}

int run_tests() {
	Pair<int, double> p1(1, 2.0);
	Pair<char, int>   p2(3, 4);

	// Explicit N=10, Ts/Us deduced: sum = 10 + (1+2) + (3+4) = 20
	int r1 = sum_with_offset<10>(p1, p2);
	if (r1 != 20) return 1;

	// Explicit N=0: sum = 0 + (1+2) + (3+4) = 10
	int r2 = sum_with_offset<0>(p1, p2);
	if (r2 != 10) return 2;

	// Three pairs: 5 + (1+2) + (3+4) + (5+5) = 25
	Pair<int, int> p3(5, 5);
	int r3 = sum_with_offset<5>(p1, p2, p3);
	if (r3 != 25) return 3;

	// Empty pack: both packs should have size 0
	int r4 = count_pairs();
	if (r4 != 0) return 4;

	return 0;
}

int main() { return run_tests(); }
