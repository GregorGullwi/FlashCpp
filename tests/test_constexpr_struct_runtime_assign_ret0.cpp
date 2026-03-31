// Test: struct returned from constexpr function assigned to a runtime (non-constexpr) variable.
//
// C++ standard: constexpr functions can return struct values.
// When assigned to a non-constexpr variable the function executes at runtime.
// The returned struct must be correctly placed in memory (including structs smaller
// than a machine word, such as a 3-byte Color).

struct Point {
	int x;
	int y;
};

struct Color {
	unsigned char r;
	unsigned char g;
	unsigned char b;
};

struct Vec3 {
	float x;
	float y;
	float z;
};

constexpr Point make_point(int x, int y) {
	return {x, y};
}

constexpr Point origin() {
	return {0, 0};
}

constexpr int point_sum(Point p) {
	return p.x + p.y;
}

constexpr Color make_color(unsigned char r, unsigned char g, unsigned char b) {
	return {r, g, b};
}

// compile-time evaluation: constexpr struct variable
constexpr Point cp = make_point(10, 20);
static_assert(cp.x == 10, "cp.x should be 10");
static_assert(cp.y == 20, "cp.y should be 20");

int main() {
	// Case 1: Point assigned from constexpr function with constant args (runtime call)
	Point p = make_point(3, 4);
	if (p.x != 3)
		return 1;
	if (p.y != 4)
		return 2;

	// Case 2: Point assigned from constexpr function with runtime variable args
	int a = 5, b = 6;
	Point q = make_point(a, b);
	if (q.x != 5)
		return 3;
	if (q.y != 6)
		return 4;

	// Case 3: struct passed to constexpr function at runtime
	int sum = point_sum(p);
	if (sum != 7)
		return 5;

	// Case 4: zero struct from constexpr function
	Point o = origin();
	if (o.x != 0)
		return 6;
	if (o.y != 0)
		return 7;

	// Case 5: 3-byte Color struct (sub-word return; tests that all 3 bytes reach the destination)
	Color c = make_color(255, 128, 0);
	if (c.r != 255)
		return 8;
	if (c.g != 128)
		return 9;
	if (c.b != 0)
		return 10;

	// Case 6: Color with runtime args
	unsigned char rv = 64, gv = 128, bv = 192;
	Color c2 = make_color(rv, gv, bv);
	if (c2.r != 64)
		return 11;
	if (c2.g != 128)
		return 12;
	if (c2.b != 192)
		return 13;

	return 0;
}
