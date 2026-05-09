struct UnusedAggregate {
	int x;
	int y;
};

constexpr UnusedAggregate gUnused{7, 9};

struct Point {
	int x;
	int y;
};

int main() {
	Point a{1, 2};
	Point b{a};						// ODR-use copy constructor
	Point c{b};
	Point d{};						// ODR-use default constructor
	d = a;							// ODR-use copy assignment
	d.x += c.y;

	return gUnused.x == 7 ? 0 : 1;
}
