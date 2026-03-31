// Test: constexpr new on a struct with a user-defined constructor but no
// default constructor must be rejected.
// Per C++20, a type with user-defined constructors is not an aggregate, so
// `new NoDflt` and `new NoDflt{}` are ill-formed (no matching 0-arg constructor).
// This file is a _fail test — compilation must fail.

struct NoDflt {
	int v;
	constexpr NoDflt(int a, int b) : v(a + b) {}
};

constexpr int make() {
	NoDflt* p = new NoDflt;	// ERROR: no default constructor
	int val = p->v;
	delete p;
	return val;
}

static_assert(make() == 0);

int main() { return 0; }
