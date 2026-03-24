// Test: constexpr global struct with float/double members initialized via paren-init.
// Previously broken: the manual ConstructorCallNode fallback path used as_int()
// which lost IEEE-754 bit patterns for float and double member types.
// Now the constexpr evaluator is tried first, which routes through
// packStructEvalResultIntoInitData / evalResultMemberToRaw for correct bit patterns.
struct Vec2f {
	float x;
	float y;
	constexpr Vec2f(float a, float b) : x(a), y(b) {}
};

struct Vec2d {
	double x;
	double y;
	constexpr Vec2d(double a, double b) : x(a), y(b) {}
};

struct Mixed {
	int   i;
	float f;
	double d;
	constexpr Mixed(int a, float b, double c) : i(a), f(b), d(c) {}
};

constexpr Vec2f gvf(1.5f, -2.25f);
constexpr Vec2d gvd(3.14, -0.5);
constexpr Mixed gm(7, 2.5f, 1.125);

static_assert(gvf.x == 1.5f);
static_assert(gvf.y == -2.25f);
static_assert(gvd.x == 3.14);
static_assert(gvd.y == -0.5);
static_assert(gm.i == 7);
static_assert(gm.f == 2.5f);
static_assert(gm.d == 1.125);

int main() {
	if (gvf.x != 1.5f) return 1;
	if (gvf.y != -2.25f) return 2;
	if (gvd.x != 3.14) return 3;
	if (gvd.y != -0.5) return 4;
	if (gm.i != 7) return 5;
	if (gm.f != 2.5f) return 6;
	if (gm.d != 1.125) return 7;
	return 0;
}
