// Phase 4B: ADL hidden friend test
// A friend function defined INSIDE the struct body is a "hidden friend":
// it is not declared in namespace scope but should be findable via ADL
// when called with an argument of the enclosing struct type.
//
namespace Lib {
struct X {
	int val;
	// Hidden friend: defined inside the struct body, not at namespace scope.
	// ADL must find Lib::getVal when called as getVal(x) with x of type Lib::X.
	friend int getVal(X x) { return x.val; }
};
} // namespace Lib

int main() {
	Lib::X x{0};
	return getVal(x);  // ADL: x is Lib::X → search Lib → find Lib::getVal → returns 0
}
