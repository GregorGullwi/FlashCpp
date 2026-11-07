// [expr.comma] gives the comma expression the type and value category of its
// last operand. [dcl.type.decltype] then sees that comma expression, not an
// unparenthesized id, so a last lvalue operand yields T& and a prvalue yields T.
int main() {
	int scalar = 0;

	static_assert(__is_same(decltype((scalar), (scalar)), int&), "parenthesized last operand is an lvalue reference");
	static_assert(__is_same(decltype(scalar, (scalar)), int&), "comma then parenthesized last operand");
	static_assert(__is_same(decltype((scalar), scalar), int&), "a bare lvalue last operand still makes the comma expression an lvalue");
	static_assert(__is_same(decltype((scalar), (scalar + 1)), int), "prvalue last operand is the value type");
	static_assert(__is_same(decltype((scalar), (scalar), (scalar)), int&), "three parenthesized operands");

	return 0;
}
