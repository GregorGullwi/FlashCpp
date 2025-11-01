// Test: Local struct declaration inside function body

int main() {
	struct Point {
		int x = 0;
		int y = 0;
	};
	
	Point p;
	p.x = 10;
	p.y = 20;
	
	return p.x + p.y;  // Should return 30
}

