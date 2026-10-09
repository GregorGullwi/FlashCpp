// A conversion operator produces a temporary. A non-const lvalue reference
// cannot bind that temporary, so the call is not viable.

struct B {
	int value;
};

struct S {
	operator B() const { return B{}; }
};

int choose(B&) { return 0; }

int call(S s) { return choose(s); }

int main() { return call(S{}); }
