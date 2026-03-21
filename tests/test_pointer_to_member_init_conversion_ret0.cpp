// Regression: pointer-to-member access should expose the member value type so
// sema can annotate arithmetic initialization conversions like int -> double.

struct Point {
	int x;
	int y;
};

int main() {
	int Point::*pm = &Point::x;
	Point p{42, 0};
	double d = p.*pm;
	return (d == 42.0) ? 0 : 1;
}
