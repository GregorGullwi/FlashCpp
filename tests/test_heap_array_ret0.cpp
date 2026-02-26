static int g = 0;

struct Point {
	int x;
	int y;
	Point() : x(10), y(20) {}
	~Point() { g += x + y; }
};

int main() {
	Point* arr = new Point[2];
	delete[] arr;
	return g == 60 ? 0 : 1;  // 2 * (10 + 20) = 60
}
