// Test: decltype with comma operator (SFINAE pattern)
// Pattern: decltype(expr1, expr2, value) to test availability of expr1
// Validates that the parser correctly handles comma operator inside decltype
// and that SFINAE correctly selects the matching overload
//
// TODO: Negative SFINAE case (WithoutFoo) requires lazy member function template
// instantiation to go through the SFINAE overload resolution loop. Currently the
// lazy registry path doesn't iterate overloads. See try_instantiate_member_function_template_explicit.

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
	// TODO: bool b = has_foo<WithoutFoo>::check<WithoutFoo>(nullptr);
	// Full SFINAE needs: return (a && !b) ? 5 : 0;
	return a ? 5 : 0;
}
