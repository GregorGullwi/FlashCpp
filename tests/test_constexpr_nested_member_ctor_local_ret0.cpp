// Test: nested member access on local constructor-initialized objects in constexpr functions.
// Previously failed with "Undefined variable in nested member access: o" because the block-scope
// initializer (parsed as InitializerListNode) triggered aggregate init instead of ctor init.

struct Inner {
	int x;
	int y;
};
struct Outer {
	Inner inner;
	constexpr Outer(int a, int b) : inner{a, b} {}
};

constexpr int test_nested_sum() {
	Outer o(3, 7);
	return o.inner.x + o.inner.y;
}
static_assert(test_nested_sum() == 10);

constexpr int test_nested_x() {
	Outer o(3, 7);
	return o.inner.x;
}
static_assert(test_nested_x() == 3);

constexpr int test_nested_y() {
	Outer o(3, 7);
	return o.inner.y;
}
static_assert(test_nested_y() == 7);

// Assign inner struct to local variable, then access its members
constexpr int test_assign_inner() {
	Outer o(5, 10);
	Inner inner = o.inner;
	return inner.x + inner.y;
}
static_assert(test_assign_inner() == 15);

// Multiple primitive + struct members
struct Config {
	int version;
	Inner offset;
	constexpr Config(int v, int ox, int oy) : version(v), offset{ox, oy} {}
};

constexpr int test_mixed_members() {
	Config cfg(42, 10, 20);
	return cfg.version + cfg.offset.x + cfg.offset.y;
}
static_assert(test_mixed_members() == 72);

// Two local constructor-initialized objects
constexpr int test_two_objects() {
	Outer a(3, 4);
	Outer b(5, 6);
	return a.inner.x + a.inner.y + b.inner.x + b.inner.y;
}
static_assert(test_two_objects() == 18);

// Constructor with computed extra field alongside nested struct
struct Container {
	Inner inner;
	int extra;
	constexpr Container(int a, int b) : inner{a, b}, extra(a + b) {}
};

constexpr int test_container() {
	Container c(4, 6);
	return c.inner.x + c.inner.y + c.extra;
}
static_assert(test_container() == 20);

// Global scope still works (was already supported)
constexpr Container gc(10, 20);
static_assert(gc.inner.x == 10);
static_assert(gc.inner.y == 20);
static_assert(gc.extra == 30);

int main() { return 0; }
