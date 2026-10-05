// [dcl.type.decltype] takes the type of the last comma-operator operand, so a
// parenthesized lvalue last operand yields T& even when an earlier operand is
// not parenthesized, while a bare or prvalue last operand yields the value type.
int main() {
	int scalar = 0;

	static_assert(__is_same(decltype((scalar), (scalar)), int&), "parenthesized last operand is an lvalue reference");
	static_assert(__is_same(decltype(scalar, (scalar)), int&), "comma then parenthesized last operand");
	static_assert(__is_same(decltype((scalar), scalar), int), "bare last operand is the value type");
	static_assert(__is_same(decltype((scalar), (scalar + 1)), int), "prvalue last operand is the value type");
	static_assert(__is_same(decltype((scalar), (scalar), (scalar)), int&), "three parenthesized operands");

	return 0;
}
