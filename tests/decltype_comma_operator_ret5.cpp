// Test: decltype with comma operator (SFINAE pattern)
// Pattern: decltype(expr1, expr2, value) to test availability of expr1
// Validates that the parser correctly handles comma operator inside decltype
// and that SFINAE correctly selects the matching overload.
// Both positive (WithFoo has foo()) and negative (WithoutFoo lacks foo()) cases.

struct WithFoo { void foo() {} };
struct WithoutFoo {};

template<typename U>
auto has_foo_check(U* u) -> decltype(u->foo(), void(), true) { return true; }

template<typename U>
auto has_foo_check(...) -> bool { return false; }

int main() {
	bool a = has_foo_check<WithFoo>(nullptr);
	bool b = has_foo_check<WithoutFoo>(nullptr);
	return (a && !b) ? 5 : 0;
}
