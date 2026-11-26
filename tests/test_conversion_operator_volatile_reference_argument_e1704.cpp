// [dcl.init.ref]/5.4.3: the same CWG 1571 rule applies to argument passing.
// operator volatile int&() cannot initialize the const int& parameter, so the
// only call candidate must be rejected instead of materializing a temporary.
struct A {
	operator volatile int&();
};

void observe(const int& value);

void use() {
	observe(A());
}
