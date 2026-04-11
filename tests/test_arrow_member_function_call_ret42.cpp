struct Point {
	int x;
	int y;

	int sum() {
		return x + y;
	}
};

int main() {
	Point point{19, 23};
	Point* pp = &point;
	return pp->sum();
}
