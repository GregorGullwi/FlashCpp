// Test: aggregate struct initialization inside constexpr functions using local bindings.
// The struct has no user-defined constructor (aggregate) so brace-init is used.
// Previously, outer_bindings (function parameters and locals) were not passed to the
// aggregate materializer, causing evaluation to fail.

struct Pt { int x; int y; };

constexpr int f(int a, int b) {
	Pt p{a, b};
	return p.x + p.y;
}

static_assert(f(3, 7) == 10);

struct Triple { int a; int b; int c; };

constexpr int g(int x, int y, int z) {
	Triple t{x, y, z};
	return t.a + t.b + t.c;
}

static_assert(g(1, 4, 5) == 10);

// Local variables as aggregate initializer arguments
constexpr int h() {
	int v1 = 3;
	int v2 = 7;
	Pt p{v1, v2};
	return p.x + p.y;
}

static_assert(h() == 10);

int main() {
	return 0;
}
