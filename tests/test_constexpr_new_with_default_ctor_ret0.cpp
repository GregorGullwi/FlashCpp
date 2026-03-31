// Test: constexpr new on a struct with a user-defined default constructor
// should work correctly — the default constructor should be invoked.

struct WithDflt {
	int v;
	constexpr WithDflt() : v(42) {}
	constexpr WithDflt(int a, int b) : v(a + b) {}
};

// new WithDflt should invoke the user-defined default constructor (v=42)
constexpr int test_new_default_ctor() {
	WithDflt* p = new WithDflt;
	int val = p->v;
	delete p;
	return val;
}
static_assert(test_new_default_ctor() == 42);

// new WithDflt(3, 7) should invoke the 2-arg constructor (v=10)
constexpr int test_new_args_ctor() {
	WithDflt* p = new WithDflt(3, 7);
	int val = p->v;
	delete p;
	return val;
}
static_assert(test_new_args_ctor() == 10);

// new on an aggregate (no user-defined constructors) should still work
struct Agg {
	int x;
	int y;
};

constexpr int test_new_aggregate() {
	Agg* p = new Agg;
	p->x = 10;
	p->y = 20;
	int val = p->x + p->y;
	delete p;
	return val;
}
static_assert(test_new_aggregate() == 30);

int main() { return 0; }
