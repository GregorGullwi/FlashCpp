// [dcl.type.decltype]: a parenthesized lvalue expression yields T&. Besides an
// id-expression, a dereference and member access on an lvalue object are
// lvalues, while a prvalue expression keeps its value type.
struct Inner {
	int value;
};

struct Outer {
	Inner inner;
};

int main() {
	int scalar = 0;
	int* pointer = &scalar;
	Outer outer{};

	static_assert(__is_same(decltype((*pointer)), int&), "dereference is an lvalue");
	static_assert(__is_same(decltype((outer.inner)), Inner&), "member access is an lvalue");
	static_assert(__is_same(decltype((outer.inner.value)), int&), "member access chain is an lvalue");
	static_assert(__is_same(decltype((scalar + 1)), int), "prvalue stays a prvalue");

	return 0;
}
