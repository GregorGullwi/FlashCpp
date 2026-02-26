static int g = 0;

struct Point {
	int x;
	int y;
	Point(int a, int b) : x(a), y(b) {}
	~Point() { g = x + y; }
};

int main() {
	Point* p = new Point(10, 20);
	delete p;
	return g == 30 ? 0 : 1;
}
