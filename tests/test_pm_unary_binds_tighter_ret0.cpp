// Regression: unary * binds tighter than pointer-to-member .*
// C++ [expr.mptr.oper] / [expr.unary.op]: *p.*pm means (*p).*pm, not *(p.*pm).

struct S {
	int v;
};

int main() {
	S s{7};
	S* p = &s;
	int S::* pm = &S::v;
	return *p.*pm - 7;
}
