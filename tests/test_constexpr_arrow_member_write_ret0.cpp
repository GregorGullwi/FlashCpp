// Test: arrow member write (p->member = value) in constexpr new/delete
// C++20 [expr.const] requires support for mutation through pointer to heap objects.

struct Point {
	int x;
	int y;
	constexpr Point(int a, int b) : x(a), y(b) {}
};

// Basic arrow write: modify both members, then read back
constexpr int arrow_write_basic() {
	Point* p = new Point(10, 20);
	p->x = 42;
	p->y = 99;
	int result = p->x + p->y;
	delete p;
	return result;  // 42 + 99 = 141
}

// Arrow compound assignment: use += through arrow
struct Counter {
	int count;
	constexpr Counter(int c) : count(c) {}
};

constexpr int arrow_compound_assign() {
	Counter* c = new Counter(0);
	c->count += 5;
	c->count += 3;
	int result = c->count;
	delete c;
	return result;  // 8
}

// Arrow write inside a loop
constexpr int arrow_write_loop() {
	Counter* c = new Counter(0);
	for (int i = 1; i <= 5; i++) {
		c->count += i;
	}
	int result = c->count;
	delete c;
	return result;  // 1+2+3+4+5 = 15
}

// Arrow write then arrow read in same expression
constexpr int arrow_write_then_read() {
	Point* p = new Point(0, 0);
	p->x = 7;
	p->y = p->x * 3;
	int result = p->x + p->y;
	delete p;
	return result;  // 7 + 21 = 28
}

// Multiple struct members written via arrow
struct Box {
	int width;
	int height;
	int depth;
	constexpr Box(int w, int h, int d) : width(w), height(h), depth(d) {}
};

constexpr int arrow_write_multiple_members() {
	Box* b = new Box(1, 1, 1);
	b->width = 3;
	b->height = 4;
	b->depth = 5;
	int result = b->width * b->height * b->depth;
	delete b;
	return result;  // 3*4*5 = 60
}

static_assert(arrow_write_basic() == 141);
static_assert(arrow_compound_assign() == 8);
static_assert(arrow_write_loop() == 15);
static_assert(arrow_write_then_read() == 28);
static_assert(arrow_write_multiple_members() == 60);

int main() {
	return 0;
}
