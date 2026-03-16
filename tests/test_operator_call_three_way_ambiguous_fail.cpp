// Test that three-way incomparable operator() overloads are rejected as ambiguous.
//
// This exercises a bug in resolve_overload where accumulated tied/incomparable
// candidates are discarded when a later candidate is strictly better than the
// current best, without re-evaluating the discarded candidates against the new best.
//
// For call f(x, y) with args (short, short):
//   A: operator()(double, short) → ranks [conversion, exact]   = [2, 0]
//   B: operator()(short, double) → ranks [exact, conversion]   = [0, 2]
//   C: operator()(int, short)    → ranks [promotion, exact]    = [1, 0]
//
// B incomparable with A: B better on arg0 (0<2), A better on arg1 (0<2) ✓
// C strictly better than A: [1,0] vs [2,0] → better on arg0, equal on arg1 ✓
// C incomparable with B: [1,0] vs [0,2] → C worse on arg0 (1>0), C better on arg1 (0<2) ✓
//
// Buggy evaluation order A, B, C:
//   best=A [2,0], tied=[A]
//   B vs A: incomparable → tied=[A,B], num=2
//   C vs A (best_ranks=[2,0]): strictly better → best=C, tied=[C], num=1
//     ← B is discarded without comparing against C!
//   Result: C selected (WRONG — should be ambiguous because C is incomparable with B)

struct ThreeWay {
	int operator()(double a, short b) { return 1; }
	int operator()(short a, double b) { return 2; }
	int operator()(int a, short b) { return 3; }
};

int main() {
	ThreeWay f;
	short x = 1;
	short y = 2;
	return f(x, y); // ambiguous: should fail to compile
}
