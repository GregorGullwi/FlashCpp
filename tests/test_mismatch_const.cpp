// Test: const qualifier mismatch
struct Point {
	int x;
	int y;
	int getSum() const;  // Declaration: const member function
};

// Definition: non-const (MISMATCH)
int Point::getSum() {
	return x + y;
}

int main() {
	Point p;
	p.x = 5;
	p.y = 10;
	return p.getSum();
}
