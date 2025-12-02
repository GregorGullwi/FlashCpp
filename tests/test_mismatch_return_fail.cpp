// Test: Return type mismatch
struct Point {
	int x;
	int y;
	int getSum();  // Declaration: returns int
};

// Definition: returns float (MISMATCH)
float Point::getSum() {
	return x + y;
}

int main() {
	Point p;
	p.x = 5;
	p.y = 10;
	return p.getSum();
}
