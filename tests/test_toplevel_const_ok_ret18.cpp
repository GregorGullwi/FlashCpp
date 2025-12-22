// Top-level const on parameters is OK to differ (doesn't affect signature)
struct Point {
	int x;
	int y;
	int add(int a);  // declaration without const
};

int Point::add(const int a) {  // definition WITH const - should be OK
	return x + y + a;
}

int main() {
	Point p;
	p.x = 5;
	p.y = 10;
	return p.add(3);  // Should return 18
}
