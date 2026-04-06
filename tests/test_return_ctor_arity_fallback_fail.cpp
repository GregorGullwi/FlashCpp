struct S {
	S(int&) {}
};

S f() {
	// Returning an rvalue must not bind to a non-const lvalue-reference
	// converting constructor during copy-initialization.
	return 1;
}

int main() {
	return 0;
}
