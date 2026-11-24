// [expr.static.cast]/5 initializes the reference directly, and [dcl.init.ref]/4
// allows that binding only between reference-compatible referents, which keeps
// cv-qualification. Casting a "const int" lvalue to "int&" therefore drops const
// and is ill-formed, while adding cv to the referent stays valid.

int main() {
	const int value = 5;
	int& reference = static_cast<int&>(value);
	(void)reference;
	return 0;
}
