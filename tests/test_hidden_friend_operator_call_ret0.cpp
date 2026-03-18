// Hidden friend operator() defined inside a class body.
// Per C++20 [class.friend]/7, this is a hidden friend — it should only be
// findable via ADL when at least one argument has the associated class type.
//
// This test verifies that the parser correctly builds the operator name
// "operator()" instead of just "operator" when the '(' token is part of
// the operator name itself.
struct Functor {
	int value;
	friend int operator()(Functor f, int x) { return f.value + x; }
};
int main() {
	Functor f;
	f.value = 3;
	// ADL should find operator() because f is of type Functor
	return f(4) - 7;  // 3 + 4 - 7 == 0
}
