// Test: use-after-free through arrow access should produce a clear diagnostic
// p->member after delete p must be diagnosed as "use after free"

struct Point {
	int x, y;
	constexpr Point(int a, int b) : x(a), y(b) {}
};

constexpr int bad_arrow_uaf() {
	Point* p = new Point(1, 2);
	delete p;
	return p->x;  // use after free - must be a compile error
}

static_assert(bad_arrow_uaf() == 1);

int main() { return 0; }
