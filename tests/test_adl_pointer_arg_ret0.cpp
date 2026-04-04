// Test: ADL with pointer and reference arguments
// C++20 [basic.lookup.argdep]/2: for pointer types, the associated
// namespaces include those of the pointed-to type.

namespace math {

struct Vec2 { int x; int y; };

int dot(Vec2* a, Vec2* b) {
	return a->x * b->x + a->y * b->y;
}

int add_x(Vec2& v, int n) {
	return v.x + n;
}

} // namespace math

int main() {
	math::Vec2 a{3, 4};
	math::Vec2 b{1, 2};
	math::Vec2* pa = &a;
	math::Vec2* pb = &b;
	// ADL should find math::dot via pointer-to-Vec2 arguments
	int d = dot(pa, pb);
	// ADL should find math::add_x via reference-to-Vec2 argument
	int s = add_x(a, 10);
	// dot(a,b) = 3*1 + 4*2 = 11, add_x(a,10) = 3+10 = 13
	return (d == 11 && s == 13) ? 0 : 1;
}
