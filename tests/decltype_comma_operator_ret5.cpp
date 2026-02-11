// Test: decltype with comma operator (SFINAE pattern)
// Pattern: decltype(expr1, expr2, value) to test availability of expr1
// Validates that the parser correctly handles comma operator inside decltype

template<typename T>
struct has_foo {
	template<typename U>
	static auto check(U* u) -> decltype(u->foo(), void(), true) { return true; }

	template<typename U>
	static auto check(...) -> bool { return false; }
};

struct WithFoo { void foo() {} };

int main() {
	bool a = has_foo<WithFoo>::check<WithFoo>(nullptr);
	return a ? 5 : 0;
}
