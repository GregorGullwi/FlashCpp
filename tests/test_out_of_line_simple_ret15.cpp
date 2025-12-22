struct Point {
	int x;
	int y;
	int getSum();
};

int Point::getSum() {
	return x + y;
}

int main() {
	Point p;
	p.x = 5;
	p.y = 10;
	return p.getSum();  // Should return 15
}
