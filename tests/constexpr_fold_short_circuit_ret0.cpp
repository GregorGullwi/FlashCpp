// Regression test: constexpr short-circuit folding for ternary and logical operators.
// The condition (or LHS of &&/||) is a compile-time constant, so only the taken branch
// is evaluated by the constexpr evaluator. The not-taken branch may call a non-constexpr
// function — without the short-circuit path the whole expression would be rejected as
// a fold candidate even though the not-taken branch is never reached.

constexpr bool kTrue = true;
constexpr bool kFalse = false;

int non_constexpr(int x) { return x + 1; }

constexpr int add(int a, int b) { return a + b; }

int main() {
	// Ternary: condition is constexpr true; false branch calls non-constexpr function.
	// Expected fold: kTrue ? add(3,4) : non_constexpr(0)  ->  7
	int v1 = kTrue ? add(3, 4) : non_constexpr(0);
	if (v1 != 7) return 1;

	// Ternary: condition is constexpr false; true branch calls non-constexpr function.
	// Expected fold: kFalse ? non_constexpr(0) : add(10,5)  ->  15
	int v2 = kFalse ? non_constexpr(0) : add(10, 5);
	if (v2 != 15) return 2;

	// Logical &&: LHS is constexpr false; RHS calls non-constexpr function.
	// Expected fold: kFalse && (non_constexpr(0) > 0)  ->  false (0)
	bool b1 = kFalse && (non_constexpr(0) > 0);
	if (b1) return 3;

	// Logical ||: LHS is constexpr true; RHS calls non-constexpr function.
	// Expected fold: kTrue || (non_constexpr(0) > 0)  ->  true (1)
	bool b2 = kTrue || (non_constexpr(0) > 0);
	if (!b2) return 4;

	// Ternary: both condition and taken branch are constexpr; not-taken has runtime call.
	// add(1,1) == 2 is constexpr true, so the false branch non_constexpr(99) is not taken.
	int v3 = (add(1, 1) == 2) ? add(20, 30) : non_constexpr(99);
	if (v3 != 50) return 5;

	return 0;
}
