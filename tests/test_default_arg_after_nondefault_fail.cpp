// Test: non-default argument after default argument should be an error
// Per C++20 [dcl.fct.default]/4: If a parameter has a default argument,
// all subsequent parameters shall also have default arguments.

int bad_func(int a = 10, int b) {  // ERROR: b has no default after a has default
	return a + b;
}

int main() {
	return bad_func(1, 2);
}
