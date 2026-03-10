struct Point {
	int x;
	int y;

	constexpr Point(int a, int b)
		: x(a), y(b) {}
};

constexpr int readBraceInitMember() {
	Point p{1, 2};
	return p.x;
}

constexpr int readCtorExprInitMember() {
	Point p = Point{3, 4};
	return p.y;
}

static_assert(readBraceInitMember() == 1);
static_assert(readCtorExprInitMember() == 4);

int main() {
	return 0;
}