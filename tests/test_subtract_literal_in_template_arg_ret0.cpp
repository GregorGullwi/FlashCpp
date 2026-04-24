// Regression test for C++20 [lex.pptoken] / [lex.ccon]:
// There are no negative numeric literals in C++. `3-1`, `a-1`, or
// `foo<3-1>` must tokenize as three tokens (`3`, `-`, `1`) so that the
// binary subtraction can be parsed. Previously the lexer eagerly consumed
// `-1` as a single negative literal token, which dropped the binary `-`
// operator in expressions like these and in particular broke template
// non-type arguments of the form `Tmpl<N - 1>`.
//
// Returns 0 on success.

template<int N>
struct tag {};

template<int N>
int foo(tag<N>, int v) { return v + N; }

int arith_at_call_site() {
	// Binary subtraction between two integer literals inside a non-type template argument:
	// must parse as tag<3 - 1> = tag<2>.
	return foo(tag<3 - 1>{}, 0);
}

int arith_between_var_and_literal() {
	int a = 5;
	// Classic case: `a-1` used to mis-tokenize to `a`, `-1` and lose the binary op.
	return a - 1;
}

int arith_between_literals_no_spaces() {
	return 3-1;
}

int main() {
	// arith_at_call_site: N=2, v=0 → returns 2
	// arith_between_var_and_literal: 5-1 = 4
	// arith_between_literals_no_spaces: 3-1 = 2
	int r = arith_at_call_site();        // 2
	r += arith_between_var_and_literal(); // +4 → 6
	r += arith_between_literals_no_spaces(); // +2 → 8
	if (r != 8) return 1;
	return 0;
}
