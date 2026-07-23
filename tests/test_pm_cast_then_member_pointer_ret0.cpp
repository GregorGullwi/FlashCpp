// Regression: cast-expression then .* is a pm-expression (not postfix).
// static_cast<T>(x).*pm must parse without requiring .* inside apply_postfix_operators.

struct S {
	int v;
};

int main() {
	S s{11};
	int S::* pm = &S::v;
	return static_cast<S&>(s).*pm - 11;
}
