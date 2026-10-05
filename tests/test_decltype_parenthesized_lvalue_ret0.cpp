// [dcl.type.decltype]: a parenthesized lvalue id-expression yields T&, while
// the unparenthesized form yields the declared type; a parenthesized prvalue
// yields the prvalue type.
struct Payload {
	int value;
};

int freeFn(int);

int main() {
	int scalar = 0;
	Payload payload{};

	static_assert(__is_same(decltype(scalar), int), "bare scalar is the declared type");
	static_assert(__is_same(decltype((scalar)), int&), "parenthesized scalar is an lvalue reference");
	static_assert(__is_same(decltype(payload), Payload), "bare record is the declared type");
	static_assert(__is_same(decltype((payload)), Payload&), "parenthesized record is an lvalue reference");
	static_assert(__is_same(decltype((freeFn)), int(&)(int)), "parenthesized function designator is a function reference");
	static_assert(__is_same(decltype((1)), int), "parenthesized literal stays a prvalue");
	static_assert(__is_same(decltype((scalar + 1)), int), "parenthesized arithmetic stays a prvalue");

	return 0;
}
