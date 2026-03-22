// Regression test: array of aggregate structs initialized with local bindings inside constexpr
struct Pt { int x; int y; };

constexpr int test() {
	int a = 1, b = 2, c = 3, d = 4;
	Pt pts[2] = {{a, b}, {c, d}};
	return pts[0].x + pts[0].y + pts[1].x + pts[1].y;  // 1+2+3+4 = 10
}
static_assert(test() == 10);

int main() { return test(); }
