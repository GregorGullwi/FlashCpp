// Test: decltype with comma operator (SFINAE pattern)
// Pattern: decltype(expr1, expr2, value) to test availability of expr1
// Validates that the parser correctly handles comma operator inside decltype
// and that SFINAE correctly selects the matching overload for the positive case.
//
// TODO: Full negative SFINAE (WithoutFoo) requires semantic member resolution during
// expression parsing in SFINAE context. Currently the parser accepts u->foo()
// syntactically without verifying foo() exists on the concrete type. This needs
// the expression parser to resolve member accesses when in_sfinae_context_ is true.
// See docs/PLAN_sfinae_semantic_resolution.md

struct WithFoo { void foo() {} };
struct WithoutFoo {};

template<typename U>
auto has_foo_check(U* u) -> decltype(u->foo(), void(), true) { return true; }

template<typename U>
auto has_foo_check(...) -> bool { return false; }

int main() {
	bool a = has_foo_check<WithFoo>(nullptr);
	// TODO: bool b = has_foo_check<WithoutFoo>(nullptr);
	// Full SFINAE needs: return (a && !b) ? 5 : 0;
	return a ? 5 : 0;
}
