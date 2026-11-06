// C++20 integral promotions apply to unary +, -, ~ and to built-in binary
// arithmetic/bitwise/shift operands before auto/decltype observes the result.
int main() {
	unsigned char a = 3;
	unsigned char b = 2;

	static_assert(__is_same(decltype(+a), int));
	static_assert(__is_same(decltype(-a), int));
	static_assert(__is_same(decltype(~a), int));
	static_assert(__is_same(decltype(!a), bool));

	static_assert(__is_same(decltype(a * b), int));
	static_assert(__is_same(decltype(a / b), int));
	static_assert(__is_same(decltype(a % b), int));
	static_assert(__is_same(decltype(a + b), int));
	static_assert(__is_same(decltype(a - b), int));
	static_assert(__is_same(decltype(a & b), int));
	static_assert(__is_same(decltype(a | b), int));
	static_assert(__is_same(decltype(a ^ b), int));
	static_assert(__is_same(decltype(a << b), int));
	static_assert(__is_same(decltype(a >> b), int));

	static_assert(__is_same(decltype(a < b), bool));
	static_assert(__is_same(decltype(a == b), bool));
	static_assert(__is_same(decltype(a && b), bool));
	static_assert(__is_same(decltype(a || b), bool));

	constexpr unsigned char high = 200;
	static_assert(high * high == 40000);
	static_assert(~high == -201);
	static_assert((high / 2) == 100);
	static_assert((high % 3) == 2);

	return 42;
}
