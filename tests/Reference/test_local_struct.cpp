// Test: Local struct declaration inside function body

int main() {
	struct Point {
		int x = 1;
		int y = 2;
	} q, r;
	
	Point p;
	p.x = 10;
	p.y = 20;
	
	return p.x + p.y + q.x + r.y;  // Should return 33
}

