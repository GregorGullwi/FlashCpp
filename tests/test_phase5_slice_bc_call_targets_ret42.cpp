// Phase 5 Slices B & C: tests that sema eagerly materializes the body of
// lazy template member functions at the moment it selects the call target
// during call-argument annotation, so codegen's normal struct visitor — not
// codegen's "make the body exist now" fallback — emits IR for the body.
//
// Covered shapes:
//   * Static member of a template struct called through qualified name (Slice B)
//   * Instance member of a template struct called through a receiver (Slice C)
//   * Nested calls where the template member also calls another template
//     member (exercises pending-root normalization after materialization)
//
// Expected return value: 42.

template <typename T>
struct Adder {
	static T plus(T a, T b) { return a + b; }
	T offset;
	explicit Adder(T o) : offset(o) {}
	T bump(T x) const { return x + offset; }
	T twice(T x) const { return plus(bump(x), bump(x)); }
};

template <typename T, int K>
struct Scaled {
	static T scale(T v) { return v * static_cast<T>(K); }
};

int main() {
	// Static template-member call through qualified name — must be materialized
	// by sema before codegen, so Adder<int>::plus has a real body.
	int a = Adder<int>::plus(20, 1);		   // 21
	if (a != 21) return 1;

	// Instance template-member call through a receiver — Slice C path.
	Adder<int> mixed(10);
	int b = mixed.bump(5);					 // 15
	if (b != 15) return 2;

	// Nested call: Adder<int>::twice -> bump + plus, both lazily materialized.
	int c = mixed.twice(3);					// (3+10) + (3+10) = 26
	if (c != 26) return 3;

	// Same shape but different primitive type to exercise per-instantiation
	// materialization rather than just the first-seen type.
	Adder<long long> wide(100LL);
	long long d = wide.twice(2LL);			 // (2+100)+(2+100) = 204
	if (d != 204LL) return 4;

	// Multi-parameter class template with NTTP — Slice B through the qualified
	// static-call path on a Scaled<T, K> instantiation.
	int e = Scaled<int, 3>::scale(7);		  // 21
	if (e != 21) return 5;

	// Sum-check so the test only returns 42 if every materialized path was
	// correct: a + (c - b) + (d - 200LL) + (e - 21) + 0 == 21 + 11 + 4 + 0 = 36
	// Use explicit arithmetic so the return expression is obviously correct.
	int total = a + (c - b) + static_cast<int>(d - 200LL) + (e - 21);   // 21 + 11 + 4 + 0
	if (total != 36) return 6;

	return 42;
}
