// Test case to verify dependent qualified calls like Template<T>::member(args)
// are deferred in template bodies and don't cause parse errors.
// This exercises the fix in Parser_Expr_PrimaryExpr.cpp where dependent
// qualified calls (e.g., __tuple_compare<_Tp, _Up, __i+1, __size>::__eq(...))
// are skipped during template definition parsing and resolved at instantiation.

template <typename T>
struct Helper {
	static int compute(int x) { return x + 1; }
};

// Template function that uses a dependent qualified call: Helper<T>::compute(...)
template <typename T>
int call_dependent(int x) {
	return Helper<T>::compute(x);
}

// Template struct with a static member function that calls another template's static member
template <typename T, typename U>
struct Comparator {
	static bool equal(T a, U b) { return a == b; }
};

template <typename T, typename U>
bool compare_via_dependent(T a, U b) {
	return Comparator<T, U>::equal(a, b);
}

int main() {
	// Test 1: Simple dependent qualified call
	int r1 = call_dependent<int>(41);
	if (r1 != 42) return 1;

	// Test 2: Two-parameter dependent qualified call
	bool r2 = compare_via_dependent<int, int>(5, 5);
	if (!r2) return 2;

	bool r3 = compare_via_dependent<int, int>(5, 6);
	if (r3) return 3;

	return 0;  // All tests passed
}
