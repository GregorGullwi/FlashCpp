// Regression test for constant folding of unary '-' / '+' on numeric literals.
//
// Now that the lexer no longer emits a synthetic negative numeric literal
// token, `-5.0`, `-0.0`, `-5`, etc. are all parsed as a unary operator
// applied to a non-negative literal. The parser must fold unary `-` / `+`
// applied directly to a NumericLiteralNode back into a single
// NumericLiteralNode with the negated value. This keeps IEEE-754 signed-zero
// semantics exact (`-0.0` really has the sign bit set) and lets the codegen
// backend treat them as immediates.
//
// Returns 0 on success.

int test_integer_literals() {
	int a = -5;
	if (a != -5) return 1;
	long b = -1234567890L;
	if (b != -1234567890L) return 2;
	unsigned int c = -1u; // well-defined: 2^32 - 1
	if (c != 4294967295u) return 3;
	long long d = -9223372036854775807LL - 1; // INT64_MIN
	if (d >= 0) return 4;
	return 0;
}

int test_double_literals() {
	double a = -5.0;
	if (a >= 0.0) return 10;
	if (a != -5.0) return 11;

	// Signed zero: `-0.0` compares equal to `0.0` but has distinct bit pattern.
	double neg_zero = -0.0;
	if (neg_zero != 0.0) return 12;

	// Contextual bool conversion on -0.0 must be false (C++20 [conv.bool]).
	if (neg_zero) return 13;

	// `!-0.0` is true (operand contextually converts to false, `!false` is true).
	if (!(!neg_zero)) return 14;

	// Negation in arithmetic expressions.
	double e = -3.5 + -1.5;
	if (e != -5.0) return 15;

	return 0;
}

int test_float_literals() {
	float a = -2.5f;
	if (a >= 0.0f) return 20;
	if (a != -2.5f) return 21;

	float neg_zero = -0.0f;
	if (neg_zero) return 22;

	return 0;
}

int test_unary_plus_literal() {
	int a = +5;
	if (a != 5) return 30;
	double b = +3.14;
	if (b != 3.14) return 31;
	return 0;
}

int test_double_negation() {
	int a = -(-5);
	if (a != 5) return 40;
	double b = -(-2.5);
	if (b != 2.5) return 41;
	// `-(-0.0)` should be `+0.0` (two sign flips).
	double c = -(-0.0);
	if (c != 0.0) return 42;
	return 0;
}

int main() {
	int r;
	r = test_integer_literals(); if (r) return r;
	r = test_double_literals();  if (r) return r;
	r = test_float_literals();   if (r) return r;
	r = test_unary_plus_literal(); if (r) return r;
	r = test_double_negation();  if (r) return r;
	return 0;
}
