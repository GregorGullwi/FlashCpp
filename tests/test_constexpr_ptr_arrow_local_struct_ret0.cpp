// Test that arrow member access works for pointers to local constexpr structs,
// both within the same scope and across constexpr function call boundaries.

struct Point {
	int x;
	int y;
	constexpr Point(int a, int b) : x(a), y(b) {}
};

// Arrow in same scope (local pointer to local constexpr struct)
constexpr int arrow_same_scope() {
	constexpr Point local{10, 20};
	const Point* pp = &local;
	return pp->x;
}
static_assert(arrow_same_scope() == 10);

// Arrow through pointer parameter (cross-scope)
constexpr int get_x(const Point* p) {
	return p->x;
}
constexpr int get_y(const Point* p) {
	return p->y;
}

constexpr int arrow_crossscope() {
	constexpr Point local{5, 15};
	return get_x(&local);
}
static_assert(arrow_crossscope() == 5);

constexpr int arrow_crossscope_y() {
	constexpr Point local{3, 7};
	return get_y(&local);
}
static_assert(arrow_crossscope_y() == 7);

// if (ptr) — valid non-null constexpr pointer is truthy
constexpr int val = 42;
constexpr bool non_null_ptr_is_true(const int* p) {
	if (p)
		return true;
	return false;
}
static_assert(non_null_ptr_is_true(&val));

int main() {
	return 0;
}
