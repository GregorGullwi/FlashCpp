// A functional cast to a reference type goes through the same reference
// binding as the explicit spelling. The alias "IntReference" collapses to
// "int&", so initializing it from a const int lvalue removes const from the
// referent and must be rejected exactly like static_cast<int&>(const int&)
// under [expr.static.cast]/5 and [dcl.init.ref]/4.

using IntReference = int&;

int main() {
	const int value = 5;
	IntReference reference = IntReference(value);
	(void)reference;
	return 0;
}
