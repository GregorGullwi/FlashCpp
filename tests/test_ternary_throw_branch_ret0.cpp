// Regression test: ternary with a throw branch must not produce
// spurious integral promotion on the non-throw branch.
// C++20 [expr.cond]/2: when one branch is a throw-expression,
// the result type is the type of the other branch unchanged.

short get_short(bool ok) {
	return ok ? (short)5 : throw 99;
}

int main() {
	short v = get_short(true);
	if (v != 5) return 1;
	return 0;
}
