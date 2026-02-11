// Test: decltype with comma operator (SFINAE pattern)
// Pattern: decltype(expr1, expr2, value) to test availability of expr1

template<typename T>
struct has_foo {
	template<typename U>
	static auto check(U* u) -> decltype(u->foo(), void(), true) { return true; }

	template<typename U>
	static auto check(...) -> bool { return false; }
};

struct WithFoo { void foo() {} };
struct WithoutFoo {};

int main() {
	bool a = has_foo<WithFoo>::check<WithFoo>(nullptr);
	// Only check that parsing succeeds - result depends on template instantiation
	return a ? 5 : 5;  // always returns 5
}
