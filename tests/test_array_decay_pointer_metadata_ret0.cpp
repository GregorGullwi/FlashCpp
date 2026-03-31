// Regression test: array-to-pointer decay must preserve multi-level pointer semantics.

struct Point {
	int x;
};

int readSecond(int** values) {
	return *values[1];
}

int readPoint(Point* points) {
	return points[0].x;
}

int main() {
	int a = 5;
	int b = 9;
	int* values[2] = {&a, &b};

	Point points[1];
	points[0].x = 33;

	if (readSecond(values) != 9)
		return 1;
	if (readPoint(points) != 33)
		return 2;
	return 0;
}