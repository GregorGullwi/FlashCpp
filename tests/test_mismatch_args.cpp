// Test: Argument count mismatch
struct Point {
	int x;
	int y;
	int add(int a);  // Declaration: 1 parameter
};

// Definition: 2 parameters (MISMATCH)
int Point::add(int a, int b) {
	return a + b;
}

int main() {
	Point p;
	return p.add(5);
}
