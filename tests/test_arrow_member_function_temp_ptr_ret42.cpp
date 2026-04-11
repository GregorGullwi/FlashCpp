struct Point {
	int x;
	int y;

	int sum() {
		return x + y;
	}
};

Point* getPoint(Point* p) {
	return p;
}

int main() {
	Point point{19, 23};
	return getPoint(&point)->sum();
}
