// [expr.static.cast]/3 lets an lvalue bind an rvalue reference directly, but
// the reverse is not a static_cast form. Binding a non-const lvalue reference
// to an xvalue is rejected by [dcl.init.ref]/5.2, because only a const and
// non-volatile lvalue reference may bind an rvalue.

int main() {
	int value = 5;
	int& reference = static_cast<int&>(static_cast<int&&>(value));
	(void)reference;
	return 0;
}
