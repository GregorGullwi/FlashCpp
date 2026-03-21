// Test consteval functions that return struct/aggregate types.
// C++20 [dcl.consteval]: consteval functions must produce a constant expression;
// they are allowed to return structured types.

struct Vec2 {
	int x, y;
};

consteval Vec2 zero_vec() {
	return {0, 0};
}

consteval Vec2 make_vec(int x, int y) {
	return {x, y};
}

// Constexpr variable from a struct-returning consteval function
constexpr Vec2 origin = zero_vec();
static_assert(origin.x == 0, "origin.x must be 0");
static_assert(origin.y == 0, "origin.y must be 0");

constexpr Vec2 v = make_vec(3, 4);
static_assert(v.x == 3, "v.x must be 3");
static_assert(v.y == 4, "v.y must be 4");

int main() {
	return origin.x + origin.y; // 0 + 0 == 0
}
